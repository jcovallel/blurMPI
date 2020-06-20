#include "FreeImage.h"
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

struct Blur_Params {
   FIBITMAP *img;
   FIBITMAP *imgAux;
   int kernel;
   unsigned ini;
   unsigned width;
   int * tosend;
};

/** FreeImage error handler @param fif Format / Plugin responsible for the error  @param message Error message */
void FreeImageErrorHandler( FREE_IMAGE_FORMAT fif, const char *message ){
   printf("\n*** ");
   if( fif != FIF_UNKNOWN ) printf( "%s Format\n", FreeImage_GetFormatFromFIF( fif ) );
   printf( "%s", message );
   printf( " ***\n" );
}

void *BlurFuncX( struct Blur_Params *arg ){

   // Inicialización de variables
   struct Blur_Params *params = (struct Blur_Params *)arg;
   FIBITMAP *imagen = params -> img;
   int kernel = params -> kernel;
   unsigned width_ini = params -> ini;
   unsigned width  = params -> width;
   unsigned total_width = FreeImage_GetWidth( imagen );
   unsigned height = FreeImage_GetHeight( imagen );
   unsigned pitch  = FreeImage_GetPitch( imagen );
   BYTE *pixelAuxDer, *pixelAuxIz, *pixel, *bits = (BYTE *)FreeImage_GetBits( imagen );
   //se mueven los apuntadores a la columna inicial del hilo
   bits = bits + ( 3 * width_ini );
   int radio = ( kernel - 1 ) / 2;
   int kernelLimit;
   // se inicializan los apuntadores a la imagen clonada para el kernel
   FIBITMAP *imagenAux = params -> imgAux;
   BYTE *bitsAux = (BYTE *)FreeImage_GetBits( imagenAux );
   bitsAux += ( 3 * width_ini );
   int sumRed, sumGreen, sumBlue, kernelCount, auxCount;

   /** Haremos primero un barrido horizontal y luego uno vertical de modo que
    * agilicemos la convolución del kernel con la imagen (si se hacen al Tiempo
    * es más lento O(K*K*h*w) vs O(2K*h*w))
    */

   //barrido horizontal
   for( int y = 0; y < height; y++ ){
      pixel = bits;
      kernelCount = 0;
      sumRed = sumGreen = sumBlue = 0;

      /** Se calcula el blur para el primer pixel (borde izquierdo)
       * El kernel de maneja sobre la imagen clonada y haciendo uso de dos apuntadores
       * uno izquierdo y uno derecho, que delimitan el inicio y el fin del vector kernel
       * (no matriz kernel ya que lo hacemos en dos barridos). El kernel se va moviento
       * sobre la imagen junto con el pixel que se está calculando.
       */
      pixelAuxDer = pixelAuxIz = bitsAux;
      kernelLimit = width_ini - radio - 1;

      // movemos el apuntador izquierdo del kernel
      auxCount = width_ini;
      while( auxCount >= 0 && auxCount > kernelLimit ){
         sumRed += pixelAuxIz[FI_RGBA_RED];
         sumGreen += pixelAuxIz[FI_RGBA_GREEN];
         sumBlue += pixelAuxIz[FI_RGBA_BLUE];
         pixelAuxIz -= 3;
         kernelCount++;
         auxCount--;
      }
      pixelAuxDer += 3;
      auxCount = width_ini + 1;
      kernelLimit = width_ini + radio + 1;
      // movemos el apuntador derecho del kernel
      while( auxCount < total_width && auxCount < kernelLimit ){
         sumRed += pixelAuxDer[FI_RGBA_RED];
         sumGreen += pixelAuxDer[FI_RGBA_GREEN];
         sumBlue += pixelAuxDer[FI_RGBA_BLUE];
         pixelAuxDer += 3;
         kernelCount++;
         auxCount++;
      }
      pixel[FI_RGBA_RED]=sumRed/kernelCount;
      pixel[FI_RGBA_GREEN]=sumGreen/kernelCount;
      pixel[FI_RGBA_BLUE]=sumBlue/kernelCount;
      pixel += 3;
      pixelAuxDer -= 3;
      pixelAuxIz += 3;
      //se calcula el blur para el resto de pixeles en la fila
      for( int x = width_ini + 1; x < width; x++ ){
         if( x - radio > 0 ){
            sumRed -= pixelAuxIz[FI_RGBA_RED];
            sumGreen -= pixelAuxIz[FI_RGBA_GREEN];
            sumBlue -= pixelAuxIz[FI_RGBA_BLUE];
            pixelAuxIz += 3;
            kernelCount--;
         }
         if( x + radio < total_width ){
            pixelAuxDer += 3;
            sumRed += pixelAuxDer[FI_RGBA_RED];
            sumGreen += pixelAuxDer[FI_RGBA_GREEN];
            sumBlue += pixelAuxDer[FI_RGBA_BLUE];
            kernelCount++;
         }
         pixel[FI_RGBA_RED]=sumRed/kernelCount;
         pixel[FI_RGBA_GREEN]=sumGreen/kernelCount;
         pixel[FI_RGBA_BLUE]=sumBlue/kernelCount;
         pixel += 3;
      }
      // siguiente línea
      bits += pitch;
      bitsAux += pitch;
   }

}

void *BlurFuncY( struct Blur_Params *arg ){
   // se inicializan apuntadores para iniciar el barrido vertical
   struct Blur_Params *params = (struct Blur_Params *)arg;
   FIBITMAP *imagen = params -> img;
   int kernel = params -> kernel;
   int send_index = 0;
   unsigned width_ini = params -> ini;
   unsigned width  = params -> width;
   int * to_send = params -> tosend;
   unsigned total_width = FreeImage_GetWidth( imagen );
   unsigned height = FreeImage_GetHeight( imagen );
   unsigned pitch  = FreeImage_GetPitch( imagen );
   BYTE *pixelAuxDer, *pixelAuxIz, *pixel, *bits = (BYTE *)FreeImage_GetBits( imagen );
   //se mueven los apuntadores a la columna inicial del hilo
   bits = bits + ( 3 * width_ini );
   int radio = ( kernel - 1 ) / 2;
   int kernelLimit;
   // se inicializan los apuntadores a la imagen clonada para el kernel
   FIBITMAP *imagenAux = params -> imgAux;
   BYTE *bitsAux = (BYTE *)FreeImage_GetBits( imagenAux );
   bitsAux += ( 3 * width_ini );
   int sumRed, sumGreen, sumBlue, kernelCount, auxCount;

   //barrido vertical
   for( int x = width_ini; x < width; x++ ){
      pixel = bits;
      kernelCount = 0;
      sumRed = sumGreen = sumBlue = 0;

      //Se calcula el blur para el primer pixel (borde superior)
      pixelAuxDer = pixelAuxIz = bitsAux;
      kernelLimit = radio - 1;
      auxCount = 0; //inicializar en height_ini si se balancea carga en el eje y
      // se mueve el apuntador izquierdo del kernel
      while( auxCount >= 0 && auxCount > kernelLimit ){
         sumRed += pixelAuxIz[FI_RGBA_RED];
         sumGreen += pixelAuxIz[FI_RGBA_GREEN];
         sumBlue += pixelAuxIz[FI_RGBA_BLUE];
         pixelAuxIz -= pitch;
         kernelCount++;
         auxCount--;
      }
      pixelAuxDer += pitch;
      auxCount = 1; //inicializar en height_ini+1 si se balancea carga en el eje y
      kernelLimit = radio + 1;
      // se mueve el apuntador derecho del kernel
      while( auxCount < height && auxCount < kernelLimit ){
         sumRed += pixelAuxDer[FI_RGBA_RED];
         sumGreen += pixelAuxDer[FI_RGBA_GREEN];
         sumBlue += pixelAuxDer[FI_RGBA_BLUE];
         pixelAuxDer += pitch;
         kernelCount++;
         auxCount++;
      }

      to_send[send_index]=sumRed/kernelCount;//pixel[FI_RGBA_RED]=sumRed/kernelCount;
      send_index++;
      to_send[send_index]=sumGreen/kernelCount;//pixel[FI_RGBA_GREEN]=sumGreen/kernelCount;
      send_index++;
      to_send[send_index]=sumBlue/kernelCount;//pixel[FI_RGBA_BLUE]=sumBlue/kernelCount;
      send_index++;
      pixel += pitch;
      pixelAuxDer -= pitch;
      pixelAuxIz += pitch;
      //se calcula el blur para el resto de pixeles en la columna
      for( int y = 1; y < height; y++ ){
         if( y - radio > 0 ){
            sumRed -= pixelAuxIz[FI_RGBA_RED];
            sumGreen -= pixelAuxIz[FI_RGBA_GREEN];
            sumBlue -= pixelAuxIz[FI_RGBA_BLUE];
            pixelAuxIz += pitch;
            kernelCount--;
         }
         if( y + radio < height ){
            pixelAuxDer += pitch;
            sumRed += pixelAuxDer[FI_RGBA_RED];
            sumGreen += pixelAuxDer[FI_RGBA_GREEN];
            sumBlue += pixelAuxDer[FI_RGBA_BLUE];
            kernelCount++;
         }
         to_send[send_index]=sumRed/kernelCount;//pixel[FI_RGBA_RED]=sumRed/kernelCount;
         send_index++;
         to_send[send_index]=sumGreen/kernelCount;//pixel[FI_RGBA_GREEN]=sumGreen/kernelCount;
         send_index++;
         to_send[send_index]=sumBlue/kernelCount;//pixel[FI_RGBA_BLUE]=sumBlue/kernelCount;
         send_index++;
         pixel += pitch;
      }
      // siguiente linea
      bits += 3;
      bitsAux += 3;
   }
}


int main( int argc, char *argv[] ){

   // verificamos si se recibieron todos los argumentos
   /*if( argc != 5 ){
      printf( "Please provide all arguments: blur-effect sourceImage outputImage kernelSize coreNum");
      exit( 1 );
   }*/
   int rank, hilos;

   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &hilos);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   // inicialización de la librería FreeImage
   FreeImage_Initialise( FALSE );

   FREE_IMAGE_FORMAT formato = FreeImage_GetFileType( argv[1], 0 );
   if( formato  == FIF_UNKNOWN ){
      FreeImage_SetOutputMessage( FreeImageErrorHandler );
   }

   // Se carga la imagen a un mapa de bits
   FIBITMAP* imagen = FreeImage_Load( formato, argv[1], 0 );
   if( !imagen ){
      FreeImage_SetOutputMessage( FreeImageErrorHandler );
   }

   unsigned total_width  = FreeImage_GetWidth(imagen); // ancho de la imagen
   unsigned total_height = FreeImage_GetHeight(imagen); // alto de la imagen
   int *retval;
   FIBITMAP *imagenAux = FreeImage_Clone( imagen ); // se clona la imagen a otro bitmap para sacar de ella los valores del kernel


   int block_width = total_width / hilos; // balanceo de carga blockwise (se divide la img por columnas)

   int *to_rcv = malloc(sizeof(int)*3*total_width*total_height);//rcv=3*total_width tosend=3*block_width
   int *to_send;

   struct Blur_Params *params = malloc( sizeof( struct Blur_Params ));
   params->img = imagen;
   params->imgAux = imagenAux;
   params->kernel = atoi(argv[3]);
   params->ini = block_width * rank;
   if(rank+1==hilos){
      params->width = total_width;
      to_send = malloc((sizeof (int)*total_height*3*(total_width-(block_width*(rank)))));
   }else{
      params->width = block_width * (rank+1);
      to_send = malloc((sizeof (int)*total_height*3*block_width));
   }
   params->tosend = to_send;

   BlurFuncX( params );

   FreeImage_Unload( imagenAux );
   imagenAux = FreeImage_Clone( imagen );

   params->imgAux = imagenAux;

   BlurFuncY( params );

   if(rank+1==hilos){
      MPI_Gather(to_send,total_height*3*(total_width-(block_width*(rank))),MPI_INT,to_rcv,total_height*3*(total_width-(block_width*(rank))),MPI_INT,0,MPI_COMM_WORLD);
   }else{
      MPI_Gather(to_send,total_height*block_width*3,MPI_INT,to_rcv,total_height*block_width*3,MPI_INT,0,MPI_COMM_WORLD);
   }

   if(rank==0){
      BYTE *bits, *bits2 = (BYTE *)FreeImage_GetBits( imagen );
      bits = bits2;
      unsigned pitch = FreeImage_GetPitch( imagen );
      int index = 0;
      for(int i = 0 ; i<total_width; i++){
         for(int k=0; k<total_height; k++){
            bits2[FI_RGBA_RED]=to_rcv[index];
            index++;
            bits2[FI_RGBA_GREEN]=to_rcv[index];
            index++;
            bits2[FI_RGBA_BLUE]=to_rcv[index];
            index++;
            bits2+=pitch;
         }
         bits+=3;
         bits2=bits;
      }
      // se guardan los cambios en una nueva imagen
      if ( FreeImage_Save( FIF_PNG, imagen, argv[2], 0 ) ) {
      }
   }

   // liberar memoria y desinicializar la librería
   free( params );
   free(to_send);
   FreeImage_Unload( imagen );
   FreeImage_Unload( imagenAux );
   FreeImage_DeInitialise( );
   MPI_Finalize();
}
