
#define CLIP(color) (unsigned char)((color>0xFF)?0xff:((color<0)?0:color))
void equalize (unsigned char *src, int width, int height, int format)
{
unsigned int histo[256];
unsigned int functequalize[256];
unsigned int meanhisto = 0;
unsigned int mean =0;
int i ,j ;
int index,U,V;
unsigned char* ptsrc = src;
unsigned char *u;
unsigned char *v;
int size ;
/*  format yuv420p */
/* compute histo */
memset(histo,0,256);
size = width*height;
 for (i=0;i<width*height;i++){
 index=*ptsrc++;
 histo[index]++;
 }
 /* normalize histo 8 bits*/
 for (i=0;i< 256;i++){
 histo[i] = (histo[i] << 8) / size;
 }
 
 /*each histo value cannot be more than width*height for 640x480 
 307200 fit in 2^19  */
 /* now compute the mean histo value so max 307200*256 fit in 2^27 */
 
 for(i=0;i<256;i++)
 	meanhisto += histo[i];
	/*normalize menahisto 10 bits*/
	// printf("meanhisto %d \n",meanhisto);
/*compute the functequalize now */
for (i=0;i <256;i++){
mean = 0;
 for(j=0;j< i;j++)
 	mean += histo[j];
if (meanhisto) functequalize[i] = CLIP(((mean << 18) / meanhisto));
else functequalize[i] = 255;
}
ptsrc = src;
u = src + (width*height);
v = u + (width*height >> 2);
/* apply the cange to y channel */
for (i = 0 ;i < width*height ; i++)
{	
	index = *ptsrc;
	 
	*ptsrc++ = (functequalize[index]);
	/*
	if (!(i & 0xFFFFFFC0)){
	U = *u-128;
	*u++ = CLIP((U * functequalize[index] >> 8)+128);
	V = *v-128;
	*v++ = CLIP((V * functequalize[index] >> 8)+128);
	}
	*/
	
}
//for (i = 0; i < width*height>>1; i++)
//	*u++= 0x80;
	
}


 
