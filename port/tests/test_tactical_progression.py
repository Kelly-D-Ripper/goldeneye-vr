"""Run the native Tactical unlock and Workshop tests with the shipped catalogue."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='Tactical package containing mods/tactical/gear')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gevr-tactical-progression-') as temp:
        run_dir = Path(temp)
        catalogue = Path('mods/tactical/gear')
        (run_dir / catalogue).mkdir(parents=True)
        for file in ('catalog.txt', 'presets.txt'):
            shutil.copyfile(args.package / catalogue / file, run_dir / catalogue / file)
        exe = run_dir / ('progression.exe' if os.name == 'nt' else 'progression')
        subprocess.run([args.cc, '-std=c11', '-O2', '-fms-extensions',
                        '-DGEVR=1', '-DPLATFORM_64BIT=1', '-D_LANGUAGE_C=1',
                        '-Iport/include', '-Iinclude', '-Isrc', '-I.',
                        'port/tests/tactical_progression_native.c',
                        'port/src/gevr_tactical_progress.c', 'port/src/gevr_tactical_gear.c',
                        '-lm', '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=run_dir, check=True)


if __name__ == '__main__':
    main()
