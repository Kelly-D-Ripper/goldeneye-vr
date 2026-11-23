#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { ROM_BYTES = 12582912, PATH_CAPACITY = 32768 };

static const wchar_t TITLE[] = L"GoldenEye Tactical 0.03";

static int message(const wchar_t *text)
{
    MessageBoxW(NULL, text, TITLE, MB_OK | MB_ICONERROR);
    return 1;
}

static BOOL normalize_rom(BYTE *rom, BOOL allow_swap)
{
    static const BYTE magic[][4] = {
        {0x80, 0x37, 0x12, 0x40},
        {0x37, 0x80, 0x40, 0x12},
        {0x40, 0x12, 0x37, 0x80}
    };
    if (memcmp(rom, magic[0], 4) != 0) {
        if (!allow_swap) return FALSE;
        unsigned mask;
        if (memcmp(rom, magic[1], 4) == 0) mask = 1;
        else if (memcmp(rom, magic[2], 4) == 0) mask = 3;
        else return FALSE;

        for (unsigned i = 0; i < ROM_BYTES; i += mask + 1) {
            for (unsigned j = 0; j <= mask / 2; ++j) {
                const BYTE first = rom[i + j];
                rom[i + j] = rom[i + mask - j];
                rom[i + mask - j] = first;
            }
        }
    }
    return memcmp(rom + 0x20, "GOLDENEYE", 9) == 0 && memcmp(rom + 0x3b, "NGEE", 4) == 0;
}

static BOOL read_rom(const wchar_t *path, BYTE *rom, BOOL allow_swap)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    LARGE_INTEGER size;
    DWORD read;
    const BOOL loaded = GetFileSizeEx(file, &size) && size.QuadPart == ROM_BYTES
        && ReadFile(file, rom, ROM_BYTES, &read, NULL) && read == ROM_BYTES;
    CloseHandle(file);
    return loaded && normalize_rom(rom, allow_swap);
}

static BOOL save_rom(const wchar_t *path, const BYTE *rom)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written;
    const BOOL saved = WriteFile(file, rom, ROM_BYTES, &written, NULL) && written == ROM_BYTES;
    CloseHandle(file);
    if (!saved) DeleteFileW(path);
    return saved;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    (void)instance;
    (void)previous;
    (void)arguments;
    (void)show;
    static wchar_t root[PATH_CAPACITY], game[PATH_CAPACITY], cache[PATH_CAPACITY], exe[PATH_CAPACITY];
    const DWORD length = GetModuleFileNameW(NULL, root, PATH_CAPACITY);
    if (!length || length >= PATH_CAPACITY) return message(L"Move the extracted folder to a shorter path.");
    wchar_t *separator = wcsrchr(root, L'\\');
    if (!separator) return message(L"Open Play GoldenEye Tactical from the extracted folder.");
    *separator = L'\0';
    if (FAILED(StringCchPrintfW(game, PATH_CAPACITY, L"%ls\\Game", root))
        || FAILED(StringCchPrintfW(exe, PATH_CAPACITY, L"%ls\\GoldenEyeVR-VR.exe", game))
        || FAILED(StringCchPrintfW(cache, PATH_CAPACITY, L"%ls\\ge.z64", game)))
        return message(L"Move the extracted folder to a shorter path.");
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES)
        return message(L"Extract the entire ZIP before running Play GoldenEye Tactical.");

    BYTE *rom = malloc(ROM_BYTES);
    if (!rom) return message(L"Could not allocate memory to read the ROM.");
    if (!read_rom(cache, rom, FALSE)) {
        if (GetFileAttributesW(cache) != INVALID_FILE_ATTRIBUTES) {
            free(rom);
            return message(L"The game folder already has an unusable ge.z64. Move that file out of Game and open Play again.");
        }

        int count;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &count);
        BOOL selected = argv && count > 1 && read_rom(argv[1], rom, TRUE);
        if (argv) LocalFree(argv);
        static wchar_t chosen[PATH_CAPACITY];
        OPENFILENAMEW picker = {
            .lStructSize = sizeof(OPENFILENAMEW),
            .lpstrFilter = L"GoldenEye 007 ROM (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0",
            .lpstrFile = chosen,
            .nMaxFile = PATH_CAPACITY,
            .lpstrInitialDir = root,
            .lpstrTitle = L"Choose your GoldenEye 007 USA ROM",
            .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT
        };
        while (!selected) {
            if (!GetOpenFileNameW(&picker)) {
                free(rom);
                return 0;
            }
            selected = read_rom(chosen, rom, TRUE);
            if (!selected) message(L"Choose a GoldenEye 007 USA cartridge dump in .z64, .v64 or .n64 format.");
        }
        if (!save_rom(cache, rom)) {
            free(rom);
            return message(L"Could not save the ROM in Game. Extract the ZIP to a folder you can write to.");
        }
    }
    free(rom);

    wchar_t command[] = L"GoldenEyeVR-VR.exe --no-launcher --unlock-missions --mirror-fullscreen "
        L"--tactical-physical --tactical-guards --tactical-mesh --tactical-interactions --tactical-enemy-rigs "
        L"--tactical-pavlov-hands --tactical-ppk --tactical-ppk-start --tactical-ppk-hand-refit "
        L"--tactical-ppk-hand-fit --tactical-grip-fit --tactical-variety --tactical-skorpion --log "
        L"--tactical-watch --tactical-gear --tactical-svd --tactical-klmk-vest --tactical-expanded "
        L"--tactical-facility-armor --tactical-heavy-guards --tactical-campaign-gear "
        L"--tactical-loadout-editor --tactical-gear-cache --tactical-rpg --tactical-source-mines --tactical-huntsman";
    STARTUPINFOW startup = {.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, 0, NULL, game, &startup, &process))
        return message(L"Could not start GoldenEye. Extract the entire ZIP, including the Game folder.");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
