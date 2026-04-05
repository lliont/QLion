/*-----------------------------------------------------------------------*/
/*    Digital sample player QLion     Liontakis Theodoulos   2025        */
/*-----------------------------------------------------------------------*/
#include "stdio.h"
#include <qdos.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
int (*_Open)(const char *, int, ...) = qopen;

#define ReadLong(a) (* (volatile unsigned long*) (a))
#define ReadByte(a) (* (volatile unsigned char*) (a))
#define ReadWord(a) (* (volatile unsigned short*) (a))
#define WriteByte(a,d) (* (volatile unsigned char*) (a) = (d))
#define WriteWord(a,d) (* (volatile unsigned short*) (a) = (d))

#define abuffer 0xC04000

char *_endmsg = NULL;
void (*consetup)() = NULL;

void sdelay()
{
unsigned long t1,t2;
t1=ReadLong(0x800030);
t2=t1;
do { t2=ReadLong(0x800030); } while (abs(t2-t1)<50L);
return;
}

void main(int argc, char *argv[])
{
	int ff, ft,mono=0;
	char fname1[80];
    char *buffer;
	unsigned int  br, i, rd1,rd2;        
	unsigned short sp, d;

	buffer=(char *) abuffer;
	printf("Playing %s\n",argv[1]);
	if (argc<3) { printf(" Usage:ew dplay;'filename frequency [mono]'\n"); return; }
	if (argc=4) { if (!strcmp("mono",argv[3])) mono=1; }
	if ((ft=open(argv[1], O_RDONLY))==-1) { printf("Can't open source file ?\n"); return; }
	br = 4096; i=1; 
	sp=atoi(argv[2]); sp=5000000/sp;
	printf("Sampling frequency 5000000/%d = %d\n",sp,(unsigned int) (5000000/sp));
	WriteWord(0x800036,sp);    
	rd1=read(ft, buffer, br);
	if (mono==1) { printf("Mode: mono \n"); WriteWord(0x800034,0x0101); } else { printf("Mode: stereo \n"); WriteWord(0x800034,0x0001); }
	do {
		rd2=read(ft, buffer+4096, br);
		if (rd1>0) {
			do { d = ReadWord(0x800034); } while(d<2047); 
		}
		rd1=read(ft, buffer, br);
		printf("%d ",i++);
		if (rd2>0) {
			do { d = ReadWord(0x800034); } while(d>2047); 
		}
	} while (rd1>0 && rd2>0);
	WriteWord(0x800034,0); 
	printf("\n");
	close(ft);
	return;
}

