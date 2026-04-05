/*-----------------------------------------------------------------------*/
/*     SD card fat directory     Liontakis Theodoulos                    */
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
#define ay_data 0x800028
#define ay_ctrl 0x80002A 
#define bfrequency 2500



char *_endmsg = NULL;
void (*consetup)() = NULL;

void sdelay()
{
unsigned long t1,t2;
t1=ReadLong(0x800030);
t2=t1;
do { t2=ReadLong(0x800030); } while (abs(t2-t1)<19L);
return;
}

void aywrite(unsigned char r, unsigned char c)
{
	    WriteByte(ay_data,r);
		WriteByte(ay_ctrl,5);
		WriteByte(ay_ctrl,5);
		WriteByte(ay_ctrl,0);
		WriteByte(ay_data,c);
		WriteByte(ay_ctrl,4);
		WriteByte(ay_ctrl,4);
		WriteByte(ay_ctrl,0);
		return;
}

void main(int argc, char *argv[])
{
	int ff, ft,mono=0;
	int key;
	char fname1[80],header[34],ddl[4];
    char *buffer, c, j;
	unsigned char attribute , v;
	unsigned long frames, ddln, freq, f, ratio, vl,v1,v2;
	unsigned int  br, i, k, rd1,rd2;        
	unsigned short digidrums, sp, d;
	WriteWord(0x80003C,10000);          /* set timer to 1 msec */
	printf("Playing %s\n",argv[1]);
	if (argc<2) { printf(" Usage: ew ymplay[,#channel];'filename [chip frequency(Khz)]'\n"); return; }
	if (argc==3) { f=atoi(argv[2]); } else f=bfrequency;
	if (f==0 || argc>3) { printf(" Usage: ew ymplay[,#channel];'filename [chip frequency(Khz)]'\n"); return; }
	if ((ft=open(argv[1], O_RDONLY))==-1) { printf("Can't open source file.\n"); return; }
	rd1=read(ft, header, 34);
	if (rd1<34) { printf("Can't read header.\n"); return; }
	if (strncmp(header,"YM6!",4)!=0 && strncmp(header,"YM5!",4)!=0) { close(ft); printf("Not a YM 5 or 6 file ?\n"); return; }
	digidrums=0;
	frames=ReadLong(&header[12]);
	digidrums= ReadWord(&header[20]);
	freq= ReadLong(&header[22])/1000L;
	printf("Original Frequency %d Khz\n",freq);
	attribute=ReadByte(&header[19]);
	if (digidrums>0)  {
		for (i=0;i<digidrums;i++) {
			read(ft,ddl,4); ddln=ReadLong(ddl);
			for (k=0;k<ddln;k++) read(ft,&c,1);
		}			
	}
	printf("%d frames, %d digidrums \n",frames,digidrums);
	do {read(ft,&c,1); printf("%c",c); } while (c!=0);
	do {read(ft,&c,1);} while (c!=0); 
	do {read(ft,&c,1);} while (c!=0);
	printf("\n");
	buffer=(char *) malloc(frames*16);
	rd1=read(ft, buffer, frames*16);
	if (rd1<frames*16) { printf("Can't read from source file.\n"); }
	if ((attribute & 1)==1) {
		printf("Interlaced \n");
		for (i=0;i<frames;i++)
		{
			for (j=0;j<14;j++) 
			{	
				if (j<6)  { 
					if (j % 2 == 1) {
						v1=(unsigned long) buffer[i+(j-1)*frames];
						v2=(unsigned long) buffer[i+j*frames];
						vl = ((v1 | (v2 << 8))* f ) / freq;
						v = (unsigned char) (vl & 0x00FF);
						aywrite(j-1,v);
						v = (unsigned char) ((vl >> 8) & 0x00F);
						aywrite(j,v);
					}
				}
				else if (j == 6)
				{
						v1=(unsigned long) (0x1F & buffer[i+6*frames]);
						vl = (v1 * f ) / freq;
						if (vl>31) v=31; else v = (unsigned char) (vl & 0x01F);
						aywrite(6,v);
				}
				else if (j == 11 || j == 12)
				{
					if (j==12) {
						v1=(unsigned long) buffer[i+11*frames];
						v2=(unsigned long) buffer[i+12*frames];
						vl = ((v2 | (v1 << 8))* f ) / freq;
						if (v1>0xFFFF) {
							aywrite(11,255);
							aywrite(12,255);
						} else {
							v = (unsigned char) (vl & 0x00FF);
							aywrite(11,v);
							v = (unsigned char) ((vl >> 8) & 0x00FF);
							aywrite(12,v);
						}
					}
				}
				else aywrite(j,buffer[i+j*frames]);
			}
			/* aywrite(7,buffer[i+7*frames]); */
			sdelay();
						key=keyrow(1);
			if (key==8) i=frames;
		}
	} else {
		printf("Not Interlaced \n");
		for (i=0;i<frames;i++)
		{
			for (j=0;j<14;j++) 
			{
				if (j<6)  { 
					if (j % 2 == 1) {
						v1=(unsigned long) buffer[i*16+j-1];
						v2=(unsigned long) buffer[i*16+j];
						vl = ((v1 | (v2 << 8))* f ) / freq;
						v = (unsigned char) (vl & 0x00FF);
						aywrite(j-1,v);
						v = (unsigned char) (vl >> 8) & 0x00F;
						aywrite(j,v);
					}
				}
				else if (j == 6)
				{
						v1=(unsigned long)(0x1F & buffer[i*16+6]);
						vl = (v1 * f ) / freq;
						if (vl>31) v=31; else v = (unsigned char) (vl & 0x001F);
						aywrite(6,v);
				}
				else if (j == 11 || j == 12)
				{
					if (j==12) {
						v1=(unsigned long) buffer[i*16+11];
						v2=(unsigned long) buffer[i*16+12];
						vl = ((v2 | (v1 << 8))* f ) / freq;
						if (v1>0xFFFF) {
							aywrite(11,255);
							aywrite(12,255);
						} else {
							v = (unsigned char) (vl & 0x00FF);
							aywrite(11,v);
							v = (unsigned char) ((vl >> 8) & 0x0FF);
							aywrite(12,v);
						}
					}
				}
				else aywrite(j,buffer[i*16+j]);
			}
			/* aywrite(7,buffer[i*16+7]); */
			sdelay();
			key=keyrow(1);
			if (key==8) i=frames;
		}
	}
	aywrite(7,255);
	free(buffer);
	printf("\n");
	close(ft);
	return;
}

