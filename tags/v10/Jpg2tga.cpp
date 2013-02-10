//------------------------------------------------------------------------------
// jpg2tga.cpp
// JPEG to TGA file conversion example program.
// Public domain, Rich Geldreich <richgel99@gmail.com>
// Last updated Nov. 26, 2010
//------------------------------------------------------------------------------
#include "picojpeg.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <assert.h>

#include "stb_image.c"
#include "timer.h"
//------------------------------------------------------------------------------
#ifndef max
#define max(a,b)    (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)    (((a) < (b)) ? (a) : (b))
#endif
//------------------------------------------------------------------------------
typedef unsigned char uint8;
typedef unsigned int uint;
//------------------------------------------------------------------------------
static int print_usage()
{
	printf("Usage: jpg2tga [source_file] [dest_file]\n");
	printf("Outputs greyscale and truecolor 24-bit TGA files.\n");
	return EXIT_FAILURE;
}
//------------------------------------------------------------------------------
static FILE *g_pInFile;
static uint g_nInFileSize;
static uint g_nInFileOfs;
//------------------------------------------------------------------------------
unsigned char pjpeg_need_bytes_callback(unsigned char* pBuf, unsigned char buf_size, unsigned char *pBytes_actually_read, void *pCallback_data)
{
   pCallback_data;
   uint n = min(g_nInFileSize - g_nInFileOfs, buf_size);
   if (n && (fread(pBuf, 1, n, g_pInFile) != n))
      return PJPG_STREAM_READ_ERROR;
   *pBytes_actually_read = (unsigned char)(n);
   g_nInFileOfs += n;
   return 0;
}
//------------------------------------------------------------------------------
// Loads JPEG image from specified file. Returns NULL on failure.
// On success, the malloc()'d image's width/height is written to *x and *y, and
// the number of components (1 or 3) is written to *comps.
// Not thread safe.
uint8 *pjpeg_load_from_file(const char *pFilename, int *x, int *y, int *comps)
{
   *x = 0;
   *y = 0;
   *comps = 0;

   g_pInFile = fopen(pFilename, "rb");
   if (!g_pInFile)
      return NULL;
   
   g_nInFileOfs = 0;

   fseek(g_pInFile, 0, SEEK_END);
   g_nInFileSize = ftell(g_pInFile);
   fseek(g_pInFile, 0, SEEK_SET);
   
   pjpeg_image_info_t image_info;
   
   timer tim;
   tim.start();

   uint8 status = pjpeg_decode_init(&image_info, pjpeg_need_bytes_callback, NULL);

   double total_decode_time = tim.get_elapsed_ms();

   if (status)
   {
      printf("pjpeg_decode_init() failed with status %u\n", status);

      fclose(g_pInFile);
      return NULL;
   }

   const uint row_pitch = image_info.m_width * image_info.m_comps;
   uint8 *pImage = (uint8 *)malloc(row_pitch * image_info.m_height);
   if (!pImage)
   {
      fclose(g_pInFile);
      return NULL;
   }
   int mcu_x = 0;
   int mcu_y = 0;

   for ( ; ; )
   {
      tim.start();
      status = pjpeg_decode_mcu();
      total_decode_time += tim.get_elapsed_ms();

      if (status)
      {
         if (status != PJPG_NO_MORE_BLOCKS)
         {
            printf("pjpeg_decode_mcu() failed with status %u\n", status);

            free(pImage);
            fclose(g_pInFile);
            return NULL;
         }

         break;
      }

      if (mcu_y >= image_info.m_MCUSPerCol)
      {
         free(pImage);
         fclose(g_pInFile);
         return NULL;
      }
            
      // Copy MCU's pixel blocks into the destination bitmap.
      uint8 *pDst_row = pImage + (mcu_y * image_info.m_MCUHeight) * row_pitch + (mcu_x * image_info.m_MCUWidth * image_info.m_comps);

      for (int y = 0; y < image_info.m_MCUHeight; y += 8)
      {
         const int by_limit = min(8, image_info.m_height - (mcu_y * image_info.m_MCUHeight + y));

         for (int x = 0; x < image_info.m_MCUWidth; x += 8)
         {
            uint8 *pDst_block = pDst_row + x * image_info.m_comps;

            uint src_ofs = (x * 8U) + (y * 16U);
            const uint8 *pSrcR = image_info.m_pMCUBufR + src_ofs;
            const uint8 *pSrcG = image_info.m_pMCUBufG + src_ofs;
            const uint8 *pSrcB = image_info.m_pMCUBufB + src_ofs;

            const int bx_limit = min(8, image_info.m_width - (mcu_x * image_info.m_MCUWidth + x));
                        
            if (image_info.m_scanType == PJPG_GRAYSCALE)
            {
               for (int by = 0; by < by_limit; by++)
               {
                  uint8 *pDst = pDst_block;

                  for (int bx = 0; bx < bx_limit; bx++)
                     *pDst++ = *pSrcR++;

                  pSrcR += (8 - bx_limit);

                  pDst_block += row_pitch;
               }
            }
            else
            {
               for (int by = 0; by < by_limit; by++)
               {
                  uint8 *pDst = pDst_block;
                  
                  for (int bx = 0; bx < bx_limit; bx++)
                  {
                     pDst[0] = *pSrcR++;
                     pDst[1] = *pSrcG++;
                     pDst[2] = *pSrcB++;
                     pDst += 3;
                  }

                  pSrcR += (8 - bx_limit);
                  pSrcG += (8 - bx_limit);
                  pSrcB += (8 - bx_limit);

                  pDst_block += row_pitch;
               }
            }
         }
         
         pDst_row += (row_pitch * 8);
      }

      mcu_x++;
      if (mcu_x == image_info.m_MCUSPerRow)
      {
         mcu_x = 0;
         mcu_y++;
      }
   }
      
   fclose(g_pInFile);
   
   *x = image_info.m_width;
   *y = image_info.m_height;
   *comps = image_info.m_comps;

   printf("Total decompression-only time: %3.3fms\n", total_decode_time);

   return pImage;
}
//------------------------------------------------------------------------------
int main(int arg_c, char *arg_v[])
{
   printf("picojpeg example v1.0 by Rich Geldreich <richgel99@gmail.com>, Compiled " __TIME__ " " __DATE__ "\n");
       
   if (arg_c != 3)
	   return print_usage();

   int n = 1;
   const char *pSrc_filename = arg_v[n++];
   const char *pDst_filename = arg_v[n++];

   printf("Source file:      \"%s\"\n", pSrc_filename);
   printf("Destination file: \"%s\"\n", pDst_filename);

   int width, height, comps;
   uint8 *pImage = pjpeg_load_from_file(pSrc_filename, &width, &height, &comps);
   if (!pImage)
   {
      printf("Failed loading source image!\n");
      return EXIT_FAILURE;
   }

   printf("Width: %i, Height: %i, Comps: %i\n", width, height, comps);

   if (!stbi_write_tga(pDst_filename, width, height, comps, pImage))
   {
      printf("Failed writing image to destination file!\n");
      return EXIT_FAILURE;
   }

   printf("Successfully wrote destination file %s\n", pDst_filename);

   free(pImage);

   return EXIT_SUCCESS;
}
//------------------------------------------------------------------------------

