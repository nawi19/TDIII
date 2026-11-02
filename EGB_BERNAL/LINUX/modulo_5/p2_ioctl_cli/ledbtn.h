#ifndef _LEDBTN_H_
#define _LEDBTN_H_

#include <linux/ioctl.h>

#define LEDBTN_MAGIC 'l'

#define LEDBTN_RESET         _IO(LEDBTN_MAGIC, 0)
#define LEDBTN_GET_BOTON     _IOR(LEDBTN_MAGIC, 1, int)
#define LEDBTN_SET_POLARIDAD _IOW(LEDBTN_MAGIC, 2, int)
#define LEDBTN_SET_LED       _IOW(LEDBTN_MAGIC, 3, int)
#define LEDBTN_XPOLARIDAD    _IOWR(LEDBTN_MAGIC, 4, int)

#endif
