#ifndef GEVR_ANDROID_PATHS_H
#define GEVR_ANDROID_PATHS_H

#ifdef GEVR_TACTICAL_QUEST
#define GEVR_ANDROID_PACKAGE "com.gevr.tactical"
#else
#define GEVR_ANDROID_PACKAGE "com.gevr.port"
#endif

#define GEVR_ANDROID_FILES "Android/data/" GEVR_ANDROID_PACKAGE "/files"
#define GEVR_ANDROID_EXTERNAL_FILES "/sdcard/" GEVR_ANDROID_FILES
#define GEVR_ANDROID_INTERNAL_FILES "/data/data/" GEVR_ANDROID_PACKAGE "/files"

#endif
