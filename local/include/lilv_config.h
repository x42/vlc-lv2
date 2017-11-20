#ifndef _LILV_CONFIG_H_
#define _LILV_CONFIG_H_

#define LILV_INTERNAL

#define HAVE_FILENO 1
#define SERD_VERSION "0.23.0"
#define HAVE_SERD 1
#define SORD_VERSION "0.15.1"
#define HAVE_SORD 1
#define SRATOM_VERSION "0.4.10"
#define HAVE_SRATOM 1

#define HAVE_CLOCK_GETTIME 1
#define LILV_VERSION "0.22.1"

#ifdef __WIN32__
# define LILV_PATH_SEP ";"
# define LILV_DIR_SEP "\\"
#else
# define LILV_PATH_SEP ":"
# define LILV_DIR_SEP "/"
# define HAVE_FLOCK 1
#endif

#if defined(__APPLE__)
# define LILV_DEFAULT_LV2_PATH "~/.lv2:~/Library/Audio/Plug-Ins/LV2:/Library/Audio/Plug-Ins/LV2"
#elif defined(__HAIKU__)
# define HAVE_POSIX_MEMALIGN 1
# define LILV_DEFAULT_LV2_PATH "~/.lv2:/boot/common/add-ons/lv2"
#elif defined(__WIN32__)
//# define LILV_DEFAULT_LV2_PATH "%APPDATA%\\LV2;%COMMONPROGRAMFILES%\\LV2;%CommonProgramFiles(x86)%\\LV2"
# define LILV_DEFAULT_LV2_PATH "%APPDATA%\\LV2;%COMMONPROGRAMFILES%\\LV2"
#else
# define LILV_DEFAULT_LV2_PATH "~/.lv2:/usr/lib/lv2:/usr/local/lib/lv2"
# define HAVE_POSIX_MEMALIGN 1
# define HAVE_POSIX_FADVISE  1
#endif

#endif /* _LILV_CONFIG_H_ */
