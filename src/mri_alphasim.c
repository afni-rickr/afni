#include "mrilib.h"
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "zgaussian.c"   /** fast function for generating Gaussian deviates **/

/*============================================================================*/
/* The function below is no longer used,
   but could be recycled if one wants to get a more AlphaSim-like result. ====*/

#if 0
intvec * ALP_count_clusters( MRI_IMAGE *bim , float rmm , int minsize )
{
   intvec *iv ;
   int nx,ny,nz , nclu ;
   MCW_cluster *clust , *mask ;
   int nxy,nxyz , ijk , ijk_last , mnum ;
   int icl , jma , ijkma ;
   float dx,dy,dz ;
   byte  *bfar ;
   short ic, jc, kc , im, jm, km, *mi,*mj,*mk ;

ENTRY("ALP_count_clusters") ;

   if( bim == NULL || bim->kind != MRI_byte ) RETURN(NULL) ;
   bfar = MRI_BYTE_PTR(bim) ;
   nx = bim->nx ; ny = bim->ny ; nz = bim->nz ;
   dx = bim->dx ; dy = bim->dy ; dz = bim->dz ;
   if( rmm <= 0.0f ){ dx = dy = dz = 1.0f ; rmm = 1.01f ; }

   /*--- make a cluster that is a mask of points closer than max_dist ---*/

   mask = MCW_build_mask( dx, dy, dz, rmm ) ;
   if( mask == NULL ){
     mask = MCW_build_mask( 1.0f,1.0f,1.0f, 1.01f ) ;
     if( mask == NULL ) RETURN(NULL) ;
   }

   nxy = nx*ny ; nxyz = nxy*nz ;

   mnum = mask->num_pt ; mi = mask->i ; mj = mask->j ; mk = mask->k ;

   /*--- scan through array, find nonzero point, build a cluster, ... ---*/

   nclu = ijk_last = 0 ; INIT_CLUSTER(clust) ; MAKE_intvec(iv,16) ;
   do {
     for( ijk=ijk_last ; ijk < nxyz && bfar[ijk] == 0 ; ijk++ ) ; /*nada*/
     if( ijk < nxyz ) bfar[ijk] = 0 ;  /* found a nonzero point */
     else             break ;          /* didn't find any ==> done */

     ijk_last = ijk+1 ;         /* start here next time */

     clust->num_pt = 0 ;        /* clear out old cluster */
     IJK_TO_THREE(ijk,ic,jc,kc,nx,nxy) ;
     ADDTO_CLUSTER_NOMAG( clust , ic,jc,kc ) ;  /* start it off */

     for( icl=0 ; icl < clust->num_pt ; icl++ ){
       ic = clust->i[icl] ;
       jc = clust->j[icl] ;
       kc = clust->k[icl] ;

       for( jma=0 ; jma < mnum ; jma++ ){
         im = ic + mi[jma] ; if( im < 0 || im >= nx ) continue ;
         jm = jc + mj[jma] ; if( jm < 0 || jm >= ny ) continue ;
         km = kc + mk[jma] ; if( km < 0 || km >= nz ) continue ;

         ijkma = THREE_TO_IJK(im,jm,km,nx,nxy) ;
         if( bfar[ijkma] == 0 ) continue ;

         ADDTO_CLUSTER_NOMAG( clust , im,jm,km ) ;
         bfar[ijkma] = 0 ;
       }
     }

     if( clust->num_pt >= minsize ){
       if( iv->nar == nclu ){ icl = 2*nclu+32; RESIZE_intvec(iv,icl); }
       iv->ar[nclu++] = clust->num_pt ;
     }

   } while( 1 ) ;

   KILL_CLUSTER(clust) ; KILL_CLUSTER(mask) ;

   if( nclu == 0 ) KILL_intvec(iv) ;
   else            RESIZE_intvec(iv,nclu) ;

   RETURN(iv) ;
}
#endif
/*============================================================================*/

/*----------------------------------------------------------------------------*/
/* Clusterize and return the size of the largest cluster in the image.
   The image is destroyed in the process; it must be comprised of bytes. -----*/

int ALP_largest_clustersize( MRI_IMAGE *bim , float rmm )
{
   int nx,ny,nz , nclu , biggest=0 ;
   MCW_cluster *clust , *mask ;
   int nxy,nxyz , ijk , ijk_last , mnum ;
   int icl , jma , ijkma , do_nn=0 ;
   float dx,dy,dz ;
   byte  *bfar ;
   short ic, jc, kc , im, jm, km, *mi,*mj,*mk ;

ENTRY("ALP_largest_clustersize") ;

   if( bim == NULL || bim->kind != MRI_byte ) RETURN(0) ;
   bfar = MRI_BYTE_PTR(bim) ;
   nx = bim->nx ; ny = bim->ny ; nz = bim->nz ;
   dx = bim->dx ; dy = bim->dy ; dz = bim->dz ;
   if( rmm <= 0.0f ){ dx = dy = dz = 1.0f ; rmm = 1.01f ; do_nn = 1 ; }

   /*--- make a cluster that is a mask of points closer than max_dist ---*/

   mask = MCW_build_mask( dx, dy, dz, rmm ) ;
   if( mask == NULL ){
     mask = MCW_build_mask( 1.0f,1.0f,1.0f, 1.01f ) ; do_nn = 1 ;
     if( mask == NULL ) RETURN(0) ;
   }

   nxy = nx*ny ; nxyz = nxy*nz ;

   mnum = mask->num_pt ; mi = mask->i ; mj = mask->j ; mk = mask->k ;

   /*--- scan through array, find nonzero point, build a cluster, ... ---*/

   nclu = ijk_last = 0 ; INIT_CLUSTER(clust) ;
   do {
     for( ijk=ijk_last ; ijk < nxyz && bfar[ijk] == 0 ; ijk++ ) ; /*nada*/
     if( ijk < nxyz ) bfar[ijk] = 0 ;  /* found a nonzero point */
     else             break ;          /* didn't find any ==> done */

     ijk_last = ijk+1 ;         /* start here next time */

     clust->num_pt = 0 ;        /* clear out old cluster */
     IJK_TO_THREE(ijk,ic,jc,kc,nx,nxy) ;
     ADDTO_CLUSTER_NOMAG( clust , ic,jc,kc ) ;  /* start it off */

     for( icl=0 ; icl < clust->num_pt ; icl++ ){
       ic = clust->i[icl] ;
       jc = clust->j[icl] ;
       kc = clust->k[icl] ;

#undef  PROC
#define PROC(p,q,r)                                    \
 ijkma = THREE_TO_IJK(p,q,r,nx,nxy) ;                  \
 if( bfar[ijkma] != 0 ){                               \
   ADDTO_CLUSTER_NOMAG(clust,p,q,r); bfar[ijkma] = 0;  \
 }

       if( do_nn ){  /*--------------------------- NN special code for speed */
         im = ic+1 ; if( im < nx ){ PROC(im,jc,kc) ; }
         im = ic-1 ; if( im >= 0 ){ PROC(im,jc,kc) ; }
         jm = jc+1 ; if( jm < ny ){ PROC(ic,jm,kc) ; }
         jm = jc-1 ; if( jm >= 0 ){ PROC(ic,jm,kc) ; }
         km = kc+1 ; if( km < nz ){ PROC(ic,jc,km) ; }
         km = kc-1 ; if( km >= 0 ){ PROC(ic,jc,km) ; }
       } else {      /*--------------------------- general rmm > 0 clustering */
         for( jma=0 ; jma < mnum ; jma++ ){
           im = ic + mi[jma] ; if( im < 0 || im >= nx ) continue ;
           jm = jc + mj[jma] ; if( jm < 0 || jm >= ny ) continue ;
           km = kc + mk[jma] ; if( km < 0 || km >= nz ) continue ;
           PROC(im,jm,km) ;
         }
       }
     }

     if( clust->num_pt > biggest ) biggest = clust->num_pt ;
   } while( 1 ) ;

   KILL_CLUSTER(clust) ; KILL_CLUSTER(mask) ;
   RETURN(biggest) ;
}

/*----------------------------------------------------------------------------*/
#define MAX_CLUSTSIZE 32768

MRI_IMAGE * mri_alphasim( int   nx , int nz ,
                          float dx , float dz  ,
                          int niter , float rmm ,
                          int num_pval , float *pval ,
                          int num_fwhm , float *fwhmx , float *fwhmz ,
                          byte *mask , long seed )
{
   MRI_IMAGE *bim , *aim , *cim ,      *dim ;
   float     *bar , *aar , *car ; byte *dar ;
   int ite , jsm , kth , nxyz , ii,jj , ny ;
   float tt , u1,u2 , *ath , nitinv , *thr , dy , sx,sz ;
   double sd ;
   static long sseed=0 ;

ENTRY("mri_alphasim") ;

   if( nx < 8 || nz < 1 ) RETURN(NULL) ;

   if( dx <= 0.0f ) dx = 1.0f ;
   if( dz <= 0.0f ) dz = dx ;

   ny = nx ; dy = dx ;

   if( niter < 1 ) niter = 1000 ;

   if( num_pval < 1 || pval == NULL ){
     static float pp = 0.001f ;
     num_pval = 1 ; pval = &pp ;
   }

   if( num_fwhm < 1 || fwhmx == NULL ){
     static float ff = 0.0f ;
     num_fwhm = 1 ; fwhmx = &ff ;
   }
   if( fwhmz == NULL ) fwhmz = fwhmx ;

   aim = mri_new_vol( MAX_CLUSTSIZE , num_fwhm , num_pval , MRI_float ) ;
   aar = MRI_FLOAT_PTR(aim) ;
#undef  ATH
#define ATH(s,t) ( aar + ((s)*MAX_CLUSTSIZE + (t)*(MAX_CLUSTSIZE*num_fwhm) -1) )

   nxyz = nx*ny*nz ;

   if( seed != 0 ){
     srand48(seed) ;
   } else if( sseed == 0 ){
     sseed = (long)time(NULL) + (long)getpid() ;
     srand48(sseed) ;
   }

   thr = (float *)malloc(sizeof(float)*num_pval) ;
   for( kth=0 ; kth < num_pval ; kth++ )
     thr[kth] = nifti_rcdf2stat( (double)pval[kth] ,
                                 NIFTI_INTENT_ZSCORE , 0.0,0.0,0.0 ) ;

   bim = mri_new_vol( nx,ny,nz , MRI_float ) ; bar = MRI_FLOAT_PTR(bim) ;
   cim = mri_new_vol( nx,ny,nz , MRI_float ) ; car = MRI_FLOAT_PTR(cim) ;
   dim = mri_new_vol( nx,ny,nz , MRI_byte  ) ; dar = MRI_BYTE_PTR (dim) ;
   dim->dx = dx ; dim->dy = dy ; dim->dz = dz ;

   /*-- iteration loop --*/

   nitinv = 1.0f / niter ;

   for( ite=0 ; ite < niter ; ite++ ){

     /*-- create uncorrelated random field --*/

     for( ii=0 ; ii < nxyz ; ii++ ) bar[ii] = zgaussian() ;

     /*-- loop over smoothings --*/

     for( jsm=0 ; jsm < num_fwhm ; jsm++ ){

       /* blur dataset */

       memcpy( car , bar , sizeof(float)*nxyz ) ;
       sx = FWHM_TO_SIGMA(fwhmx[jsm]) ;
       sz = FWHM_TO_SIGMA(fwhmz[jsm]) ;
       if( sx > 0.0f || sz > 0.0f )
         EDIT_blur_volume_3d( nx,ny,nz      , dx,dy,dz ,
                              MRI_float,car , sx,sx,sz  ) ;

       /* find stdev of blurred dataset (we know the mean is zero) */

       sd = 0.0 ;
       for( ii=0 ; ii < nxyz ; ii++ ) sd += car[ii]*car[ii] ;
       sd = sqrt(sd/nxyz) ;

       /* mask blurred dataset */

       if( mask != NULL )
         for( ii=0 ; ii < nxyz ; ii++ ) if( mask[ii] == 0 ) car[ii] = 0.0f ;

       /*-- loop over per-voxel thresholds --*/

       for( kth=0 ; kth < num_pval ; kth++ ){

          /* threshold */

          tt = sd * thr[kth] ;
          for( ii=0 ; ii < nxyz ; ii++ ) dar[ii] = (car[ii] >= tt) ;

          /* clusterize and count into aar */

          jj = ALP_largest_clustersize( dim , rmm ) ;
          if( jj > 0 ){
            if( jj > MAX_CLUSTSIZE ) jj = MAX_CLUSTSIZE ;
            ath = ATH(jsm,kth); ath[jj] += 1.0f ;
          }

       } /* end of loop over thresholds */
     } /* end of loop over smoothings */
   } /* end of iterations */

   mri_free(dim) ; mri_free(cim) ; mri_free(bim) ; free((void *)thr) ;

   /* convert x-th entry to prob(largest cluster size >= x) */

   for( jsm=0 ; jsm < num_fwhm ; jsm++ ){
     for( kth=0 ; kth < num_pval ; kth++ ){
       ath = ATH(jsm,kth) ;
       for( jj=MAX_CLUSTSIZE-1 ; jj >= 1 ; jj-- ) ath[jj] += ath[jj+1] ;
       for( jj=1 ; jj <= MAX_CLUSTSIZE ; jj++ ) ath[jj] *= nitinv ;
     }
   }

   RETURN(aim) ;
}

/**********************************************************/

int main( int argc , char *argv[] )
{
   int nx,ny,nz , niter , jsm,kth,ii,jj,kk,qq ;
   float dx,dy,dz , rmm ;
   MRI_IMAGE *aim ; float *aar,*ath ;

   float pval[28] ; int num_pval=28 ;
   float fwhm[21] ; int num_fwhm=21 ; float dfwhm=0.25f ;
   MRI_IMAGE *maskim ; byte *mask, *mmm ;

   NI_element *nel ;
   NI_stream ns ;
   int clast , cfirst , cnum ;
   char atr[6666] ;

   float alph[ 3] = { 0.01f , 0.05f , 0.10f } ;
   int num_alph= 3 ;
   int qaa ; float aa , ff ;
   MRI_IMAGE *alpim ; float *alpar ;

   if( argc < 4 ){
     printf("args: nx nz niter\n") ; exit(0) ;
   }

   ny = nx = (int)strtod(argv[1],NULL); if( nx    < 8 ) ERROR_exit("nx bad")   ;
   nz      = (int)strtod(argv[2],NULL); if( nz    < 1 ) ERROR_exit("nz bad")   ;
   niter   = (int)strtod(argv[3],NULL); if( niter < 1 ) ERROR_exit("niter bad");

   for( ii=0 ; ii < num_pval ; ii++ ) pval[ii] = (float)pow(10.0,0.1*ii-4.0) ;
   for( ii=0 ; ii < num_fwhm ; ii++ ) fwhm[ii] = ii*dfwhm ;

   dx = dy = dz = 1.0f ; rmm = 0.0f ;

#if 1
   maskim = mri_new_vol(nx,ny,nz,MRI_byte) ; mask = MRI_BYTE_PTR(maskim) ;
   jsm = 1+nx*nx/4 ; kth = nx/2 ;
   for( kk=0 ; kk < nz ; kk++ ){
     for( jj=0 ; jj < ny ; jj++ ){
       mmm = mask + (kk*nx*ny + jj*nx) ; qq = jsm - (jj-kth)*(jj-kth) ;
       for( ii=0 ; ii < nx ; ii++ )
         mmm[ii] = ( (ii-kth)*(ii-kth) <= qq ) ;
     }
   }
   INFO_message("%d voxels in mask",THD_countmask(nx*ny*nz,mask)) ;
#else
   maskim = NULL ; mask = NULL ;
#endif

   aim = mri_alphasim( nx,nz , dx,dz , niter,rmm ,
                       num_pval,pval , num_fwhm,fwhm,NULL , mask , 0 ) ;

   INFO_message("simulation done: CPU=%g",COX_cpu_time()) ;

   mri_free(maskim) ;

   if( aim == NULL ) ERROR_exit("aim bad") ;
   aar = MRI_FLOAT_PTR(aim) ;

   ns = NI_stream_open( "fd:1" , "w" ) ; if( ns == NULL ) ERROR_exit("bad ns") ;
   for( qaa=0 ; qaa < num_alph ; qaa++ ){
     aa    = alph[qaa] ;
     alpim = mri_new( num_pval , num_fwhm , MRI_float ) ;
     alpar = MRI_FLOAT_PTR(alpim) ;
     for( jsm=0 ; jsm < num_fwhm ; jsm++ ){
       for( kth=0 ; kth < num_pval ; kth++ ){
         ath = ATH(jsm,kth) ;
         for( ii=MAX_CLUSTSIZE ; ii > 0 && ath[ii] < aa ; ii-- ) ; /*nada*/
         if( ii == 0 ){
           alpar[kth+jsm*num_pval] = 0.0f ;     /* failed to find cutoff */
         } else if( ii == MAX_CLUSTSIZE ){
           alpar[kth+jsm*num_pval] = MAX_CLUSTSIZE ; /* shouldn't happen */
         } else {
           ff = (ath[ii]-aa)/(ath[ii]-ath[ii+1]) ;  /* ath[ii] >= aa > ath[ii+1] */
           alpar[kth+jsm*num_pval] = ii+ff ;
         }
     }}
     nel = NI_new_data_element( "AlphaSim" , num_pval ) ;
     for( jsm=0 ; jsm < num_fwhm ; jsm++ )
       NI_add_column( nel , NI_FLOAT , alpar+jsm*num_pval ) ;
     sprintf(atr,"%d %d %g",nx,nz,aa) ;
     NI_set_attribute( nel , "NxNzAlpha" , atr ) ;

     atr[0] = '\0' ;
     for( kth=0 ; kth < num_pval ; kth++ ) sprintf(atr+strlen(atr),"%g ",pval[kth]) ;
     atr[strlen(atr)-1] = '\0' ; NI_set_attribute( nel , "Pval" , atr ) ;

     atr[0] = '\0' ;
     for( jsm=0 ; jsm < num_fwhm ; jsm++ ) sprintf(atr+strlen(atr),"%g ",fwhm[jsm]) ;
     atr[strlen(atr)-1] = '\0' ; NI_set_attribute( nel , "Fwhm" , atr ) ;

     NI_write_element( ns , nel , NI_TEXT_MODE ) ;
     NI_free_element( nel ) ; mri_free(alpim) ;
   }

   NI_stream_close( ns ) ;
   exit(0) ;
}
