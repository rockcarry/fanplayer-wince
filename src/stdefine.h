/* ��׼ͷ�ļ� */
#ifndef __STDEFINE_H__
#define __STDEFINE_H__

// headers
#include <windows.h>
#include <inttypes.h>

#ifndef PATH_MAX
#define PATH_MAX   MAX_PATH
#endif

#ifndef strcasecmp
#define strcasecmp stricmp
#endif

#define DO_USE_VAR(a) do { a = a; } while (0)

#endif


