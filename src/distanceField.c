/********************************************************************************************************

NAME:
    distanceField - determines depth of voxels in 3D binary objects

SYNOPSIS:
    distanceField -i <input filename> [-m <metric>][-s <0|1>][-e <0|1>]

The input file is expected to be an AFNI dataset.

The "metric" is a text string (upper case) describing the algorithm used to estimate the depth. It may be
one of the following.
MARCHING_PARABOLAS - Marching parabolas (default)
EROSION - Erosion algorithm.

Optional arguments specifically for MARCHING_PARABOLAS:
    s: Square root the output
    e: Treat edge of field of view as zero

*********************************************************************************************************/

#include <stdio.h>
#include <dirent.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "mrilib.h"
#include "distanceField.h"
#include <float.h>

#define PROCESS_ROIS_SEPARATELY 0   // Applies to marching parabolas
#define BIG 10000000000.0

typedef float flt;

typedef enum METRIC_TYPE {
         MARCHING_PARABOLAS,
         EROSION
 } METRIC_TYPE ;


int Cleanup(char *inputFileName, THD_3dim_dataset *din);
static int afni_edt(THD_3dim_dataset * din, float *outImg, bool do_sqrt, bool edges_are_zero_for_nz);
static int erosion(THD_3dim_dataset * din, float *outImg);
int open_input_dset(THD_3dim_dataset ** din, char * fname);
int outputDistanceField(float *outImg, THD_3dim_dataset *din, int metric);
int outputDistanceFieldDebug(float *outImg, THD_3dim_dataset *din, int metric);
int doesFileExist(char * searchPath, char * prefix,char *appendage , char * outputFileName);
void edt1_local(THD_3dim_dataset * din, flt * df, int n);
void edt_local(float scale, flt * f, int n);
float sqr(float x);
static flt vx(flt * f, int p, int q);
bool sixConnectedAllHi(BYTE *buffer, int index, int nx, int ny, int nz);
int getIndex(int x, int y, int z, int nx, int ny, int nz);
int transposeYZ(float *volume, int nx, int ny, int nz);
int testTransposeFunction(THD_3dim_dataset * din);
int outputTransposedDataSet(float *buffer, THD_3dim_dataset *din, int nx, int nz, int ny);
int shortToByte(THD_3dim_dataset ** din);
ERROR_NUMBER getNonzeroIndices(int nvox, int *inputImg, int *numIndices, int **indices);
ERROR_NUMBER processIndex(int index, int *inputImg, float **outImg, THD_3dim_dataset *din);
int usage();
ERROR_NUMBER img3d_Euclidean_DT(int *im, int nx, int ny, int nz,
                       bool do_sqrt, bool edges_are_zero_for_nz, float *ad3, float *odt);
ERROR_NUMBER run_EDTD_per_line(int *roi_line, float *dist2_line, int Na,
                       float delta, bool edges_are_zero_for_nz);
float * Euclidean_DT_delta(float *f, int n, float delta);

// Debugging variables
int debugNx, debugNy, debugNz;
float   debugScaleFactors[3], *debugOutImage;

float sqr(float x){
    return x*x;
}

int main( int argc, char *argv[] )
{
    char    *inputFileName=NULL;
    int     i, metric=MARCHING_PARABOLAS;
    THD_3dim_dataset * din = NULL;
    ERROR_NUMBER    errorNumber;
    float *outImg;
    bool    do_sqrt=TRUE, edges_are_zero_for_nz=TRUE;

    for (i=0; i<argc; ++i) if (argv[i][0]=='-'){
        switch(argv[i][1]){
        case 'e':
            edges_are_zero_for_nz = atoi(argv[++i]);
            break;
        case 'i':
            if (!(inputFileName=(char*)malloc(strlen(argv[++i])+8)))
                return Cleanup(inputFileName,  din);
            sprintf(inputFileName,"%s",argv[i]);
            break;

        case 'm':
            if (!strcmp(argv[++i],"MARCHING_PARABOLAS")){
                metric = MARCHING_PARABOLAS;
            }
            if (!strcmp(argv[i],"EROSION")){
                metric = EROSION;
            } else {
                fprintf(stderr, "Unrecognized metric");
                if (inputFileName) free(inputFileName);
                return ERROR_UNRECOGNIZABLE_SPECIES;
            }
            break;
        case 's':
            do_sqrt = atoi(argv[++i]);
            break;
        default:
                return usage();
        }
    }

    if (!inputFileName) return usage();

    // Open dataset
    if ( (errorNumber=open_input_dset(&din, inputFileName))!=ERROR_NONE ){
        Cleanup(inputFileName, din);
        return errorNumber;
    }

    if (!(outImg = (float *)calloc(DSET_NVOX(din), sizeof(float)))){
        Cleanup(inputFileName, din);
        return ERROR_MEMORY_ALLOCATION;
    }

    // Apply metric
    switch (metric){
    case MARCHING_PARABOLAS:
        if ((errorNumber=afni_edt(din, outImg, do_sqrt, edges_are_zero_for_nz))!=ERROR_NONE){
            Cleanup(inputFileName, din);
            return errorNumber;
        }
        break;
    case EROSION:
        if ((errorNumber=erosion(din, outImg))!=ERROR_NONE){
            Cleanup(inputFileName, din);
            return errorNumber;
        }
        break;
    }


#define DEBUG     1
#if DEBUG
    // Output result to afni dataset
    free(outImg);
    outImg = debugOutImage;
    outputDistanceFieldDebug(outImg, din, metric);
#else
    // Output result to afni dataset
    outputDistanceField(outImg, din, metric);
#endif

    Cleanup(inputFileName,  din);

    return 0;
}

int usage(){
    fprintf(stderr, "SYNOPSIS:\n");
        fprintf(stderr, "\tdistanceField -i <input filename> [-m <metric>]\n\n");

    fprintf(stderr, "The input file is expected to be an AFNI dataset.\n\n");

    fprintf(stderr, "The \"metric\" is a text string (upper case) describing the algorithm used to\n");
    fprintf(stderr, "estimate the depth. It may be one of the following.\n");
    fprintf(stderr, "MARCHING_PARABOLAS - Marching parabolas (default)\n");
    fprintf(stderr, "EROSION - Erosion algorithm.\n\n");

    fprintf(stderr, "Optional arguments specifically for MARCHING_PARABOLAS:\n");
    fprintf(stderr, "\ts: Square root the output\n");
    fprintf(stderr, "\te: Treat edge of field of view as zero\n");

    return 0;
}

int outputDistanceFieldDebug(float *outImg, THD_3dim_dataset *din, int metric){

    char *prefix=DSET_PREFIX(din);
    char *searchPath=DSET_DIRNAME(din);
    char *outputFileName;
    char  appendage[256];
/**/
    // Set appendage
    switch (metric){
    case MARCHING_PARABOLAS:
        sprintf(appendage, "MarParabDebug");
        break;
    case EROSION:
        sprintf(appendage, "ErosionDebug");
        break;
    }

    // Allocate memory to output name buffer
    if (!(outputFileName=(char *)malloc(strlen(searchPath)+strlen(prefix)+strlen(appendage)+8))){
       return ERROR_MEMORY_ALLOCATION;
    }

    // Determine whether output file already exists
    int outputFileExists = doesFileExist(searchPath,prefix,appendage,outputFileName);

    // Set output dimensions
    THD_ivec3 nxyz={debugNx, debugNy, debugNz};
    THD_fvec3 xyzdel = {debugScaleFactors[2], debugScaleFactors[1], debugScaleFactors[0]};

    // Output Fourier spectrum image (if it does not already exist)
    if (!outputFileExists){
        THD_3dim_dataset *dout = EDIT_empty_copy(din);
        sprintf(outputFileName,"%s%s%s",searchPath,prefix,appendage);
        EDIT_dset_items( dout ,
                        ADN_prefix, outputFileName,
                        ADN_type, MRI_float,
                        ADN_nxyz, nxyz,
                        ADN_xyzdel, xyzdel,
                        ADN_none ) ;
        EDIT_substitute_brick(dout, 0, MRI_float, outImg);
        DSET_write(dout);
    }

    // Cleanup
    free(outputFileName);
/**/
    return ERROR_NONE;
}

int outputDistanceField(float *outImg, THD_3dim_dataset *din, int metric){
    char *prefix=DSET_PREFIX(din);
    char *searchPath=DSET_DIRNAME(din);
    char *outputFileName;
    char  appendage[256];

    // Set appendage
    switch (metric){
    case MARCHING_PARABOLAS:
        sprintf(appendage, "MarParab");
        break;
    case EROSION:
        sprintf(appendage, "Erosion");
        break;
    }

    // Allocate memory to output name buffer
    if (!(outputFileName=(char *)malloc(strlen(searchPath)+strlen(prefix)+strlen(appendage)+8))){
       return ERROR_MEMORY_ALLOCATION;
    }

    // Determine whether output file already exists
    int outputFileExists = doesFileExist(searchPath,prefix,appendage,outputFileName);

    // Output Fourier spectrum image (if it does not already exist)
    if (!outputFileExists){
        THD_3dim_dataset *dout = EDIT_empty_copy(din);
        sprintf(outputFileName,"%s%s%s",searchPath,prefix,appendage);
        EDIT_dset_items( dout ,
                        ADN_prefix, outputFileName,
                        ADN_type, MRI_float,
                        ADN_none ) ;
        EDIT_substitute_brick(dout, 0, MRI_float, outImg);
        DSET_write(dout);
    }

    // Cleanup
    free(outputFileName);

    return ERROR_NONE;
}

static int erosion(THD_3dim_dataset * din, float *outImg){

    // Get dimensions in voxels
    int nz = DSET_NZ(din);
    int ny = DSET_NY(din);
    int nx = DSET_NX(din);
    int nvox = nx*ny*nz;
    int i;
    bool objectVoxelsLeft=TRUE;
    BYTE * buffer;

/*
    // Get real world voxel sizes
    int yDim = fabs(DSET_DY(din));
    int zDim = fabs(DSET_DZ(din));
    */

	if ((nvox < 1) || (nx < 2) || (ny < 2) || (nz < 1)) return ERROR_DIFFERENT_DIMENSIONS;

    int brickType=DSET_BRICK_TYPE(din, 0);
    if( brickType != MRI_byte ) return ERROR_DATATYPENOTHANDLED;
    BYTE * img = DSET_ARRAY(din, 0);

    // Add uneroded volume to output
    for (i=0; i<nvox; ++i) outImg[i]+=(img[i]!=0);

    // Allocate memory to buffer
    if (!(buffer = (BYTE *)malloc(nvox*sizeof(BYTE)))) return ERROR_MEMORY_ALLOCATION;

    // Erode volume, adding eroede volume to output until no object voxels left
    do {
        objectVoxelsLeft = FALSE;

        // Copy inpout image to buffer
        for (i=0; i<nvox; ++i) buffer[i] = img[i];

        // Erode input
        for (i=0; i<nvox; ++i){
            img[i]=(buffer[i]!=0) && sixConnectedAllHi(buffer, i, nx, ny, nz);
            if (img[i]) objectVoxelsLeft = TRUE;
        }

        // Add eroded volume to output
        for (i=0; i<nvox; ++i) outImg[i]+=(img[i]!=0);
    } while (objectVoxelsLeft);

    // Cleanup
    free(buffer);


    return ERROR_NONE;
}

bool sixConnectedAllHi(BYTE *buffer, int index, int nx, int ny, int nz){
    int planeSize = nx*ny;
    int z = (int)(index/planeSize);
    int y = (int)((index-z*planeSize)/nx);
    int x = index - z*planeSize - y*nx;

    return buffer[getIndex((x>0)? x-1:x,y,z, nx, ny,nz)]  && buffer[getIndex((x<nx-1)? x+1:x,y,z, nx, ny,nz)] &&
        buffer[getIndex(x, (y>0)? y-1:y,z, nx, ny,nz)] && buffer[getIndex(x, (y<ny-1)? y+1:y,z, nx, ny,nz)] &&
        buffer[getIndex(x,y,(z>0)? z-1:z, nx, ny,nz)] && buffer[getIndex(x,y,(z<nz-1)? z+1:z, nx, ny,nz)] ;
}

int getIndex(int x, int y, int z, int nx, int ny, int nz){
    return z*nx*ny + y*nx + x;
}


static int afni_edt(THD_3dim_dataset * din, float *outImg, bool do_sqrt, bool edges_are_zero_for_nz){

    // Get dimensions in voxels
    int nz = DSET_NZ(din);
    int ny = DSET_NY(din);
    int nx = DSET_NX(din);
    int nvox = nx*ny*nz;
    int *inputImg;
    int *indices;
    BYTE * byteImg;
    short * shortImg;
    float   *floatImg;

	if ((nvox < 1) || (nx < 2) || (ny < 2) || (nz < 1)) return ERROR_DIFFERENT_DIMENSIONS;

	// Alliocate memory to integer input buffer
	if (!(inputImg=(int *)calloc(nvox,sizeof(int)))) return ERROR_MEMORY_ALLOCATION;

	DSET_load(din);
    int brickType=DSET_BRICK_TYPE(din, 0);
    fprintf(stderr, "brickType=%d\n", brickType);
    switch(brickType){
        case MRI_byte:
            byteImg = DSET_ARRAY(din, 0);
            for (int i=0; i<nvox; ++i) inputImg[i] = byteImg[i];
            break;
        case MRI_short:
            shortImg = DSET_ARRAY(din, 0);
            for (int i=0; i<nvox; ++i) inputImg[i] = shortImg[i];
            break;
        case MRI_int:
            free(inputImg);
            inputImg = DSET_ARRAY(din, 0);
            break;
        case MRI_float:
            floatImg = DSET_ARRAY(din, 0);
            for (int i=0; i<nvox; ++i) inputImg[i] = (int)(floatImg[i]+0.5);
            break;
    }

    // DEBUG: Make test volume
    // make a test image: a map of various ROIs
    debugNz = nz = 80;
    debugNy = ny = 40;
    debugNx = nx = 20;
    nvox = nx*ny*nz;
    int *vol;
    vol = (int *)calloc(nx*ny*nz, sizeof(int));
    //free(outImg);
    outImg = (float *)calloc(nvox,sizeof(float));
    debugOutImage = outImg;
    float ad3[3] = {0.5, 1.0, 2.0};
    memcpy(debugScaleFactors, ad3, 3*sizeof(float));

    // LX, LY, LZ = np.shape(vol)

    int planesize=nx*ny;

    for (int z=4; z<18; ++z){
        int zOffset=z*planesize;
        for (int y=2; y<8; ++y){
            int yOffset = zOffset + y*nx;
            for (int x=4; x<8; ++x){
                vol[yOffset+x] = 1;
                fprintf(stderr, "%d,", yOffset+x);
            }
        }
    }

    // vol[4:18, 2:8, 4:8]     = 1
    /*
    vol[2:8, 4:18, 4:8]     = 4
    vol[21:30, 0:10, 11:14] = 7
    for i in range(LX):
        for j in range(LY):
            for k in range(LZ):
                if (15-i)**2 + (25-j)**2 + (10-k)**2 < 31:
                    vol[i,j,k] = 2
    vol[17:19, :, 3:6]           = 1
    vol[:, 19:21, 3:6]           = 10
    vol[60:70,28:36,14:19]       = 17
    for i in range(LX):
        for j in range(LY):
            for k in range(LZ):
                if (65-i)**2 + (10-j)**2 + (10-k)**2 < 75:
                    if not((60-i)**2 + (7-j)**2 + (10-k)**2 < 31):
                        vol[i,j,k] = 2

    vol[40:56, 25:33, 4:8]     = 5    # a square depending on vox size
*/

#if PROCESS_ROIS_SEPARATELY
    // Get unique nonzero index values
    if ((errorNumber=getNonzeroIndices(nvox, inputImg, &numIndices, &indices))!=ERROR_NONE){
        free(inputImg);
        return errorNumber;
    }

    // Process each index
    for (int i=0; i<numIndices; ++i){
        if ((errorNumber=processIndex(indices[i], inputImg, &addend, din))!=ERROR_NONE){
            free(inputImg);
            free(indices);
            return errorNumber;
        }
        for (int j=0; j<nvox; ++j) outImg[j]+=addend[j];
    }
#else
    // Get real world voxel sizes
    // DEBUG float ad3[3]={fabs(DSET_DX(din)), fabs(DSET_DY(din)), fabs(DSET_DZ(din))};

    inputImg = vol; // DEBUG
    img3d_Euclidean_DT(inputImg, nx, ny, nz,
                       do_sqrt, edges_are_zero_for_nz, ad3, outImg);
#endif

	// Cleanup
	free(inputImg);
	free(indices);

	return ERROR_NONE;
}

ERROR_NUMBER img3d_Euclidean_DT(int *im, int nx, int ny, int nz,
    bool do_sqrt, bool edges_are_zero_for_nz, float *ad3, float *odt){

    int nvox = nx*ny*nz;           // number of voxels
    int planeSize = nx*ny;

    // initialize the "output" or answer array
    for (int i=0; i<nvox; ++i) odt[i] = (im[i]>0)? BIG : 0;

    // first pass: start with all BIGs (along x-axes)
    int *inRow = im;
    float *outRow = odt;
    for (int z = 0; z <nz; ++z){
        for (int y = 0; y < ny; ++y){
            // Calc with it, and save results
            run_EDTD_per_line( inRow, outRow, nx, ad3[2], edges_are_zero_for_nz) ;

            // Increment row
            inRow += nx;
            outRow += nx;
        }
    }

    if (!(inRow=(int *)malloc(ny*sizeof(int)))) return ERROR_MEMORY_ALLOCATION;
    if (!(outRow=(float *)calloc(ny,sizeof(float)))) {
        free(inRow);
        return ERROR_MEMORY_ALLOCATION;
    }
    for (int z = 0; z < nz; ++z){
        int *inPlane = im + (z*planeSize);
        float *outPlane = odt + (z*planeSize);
        for (int x = 0; x < nx; ++x){
            // get a line...
            for (int y=0; y<ny; ++y){
                int offset = (y*nx) + x;
                inRow[y] = inPlane[offset ];
                outRow[y] = outPlane[offset ];
            }

            // ... and then calc with it, and save results
            run_EDTD_per_line( inRow, outRow, ny, ad3[1], edges_are_zero_for_nz) ;

            // Record new output row
            for (int y=0; y<ny; ++y){
                int offset = (y*nx) + x;
                outPlane[offset] = outRow[y];
            }
        }
    }
    free(outRow);
    free(inRow);


    // 2nd pass: start from previous; any other dimensions would carry
    // on from here
    if (!(inRow=(int *)malloc(nz*sizeof(int)))) return ERROR_MEMORY_ALLOCATION;
    if (!(outRow=(float *)calloc(nz, sizeof(float)))) {
        free(inRow);
        return ERROR_MEMORY_ALLOCATION;
    }
    for (int y = 0; y < ny; ++y){
        for (int x = 0; x < nx; ++x){
            int planeOffset = (y*nx) + x;
            // get a line...
            for (int z=0; z<nz; ++z){
                int offset = planeOffset + (z*planeSize);
                inRow[z] = im[offset] ;
                outRow[z] = odt[offset] ;
            }

            // ... and then calc with it, and save results
            run_EDTD_per_line( inRow, outRow, nz, ad3[0], edges_are_zero_for_nz) ;

            // Record new output row
            for (int z=0; z<nz; ++z) {
                int offset = planeOffset + (z*planeSize);
                odt[offset] = outRow[z];
            }
        }
    }
    free(outRow);
    free(inRow);
/*
    for jj in range(SH[1]) :
        for kk in range(SH[2]) :
                # get a line...
                aa = im[:,jj,kk]
                # ... and then calc with it, and save results
                odt[:,jj,kk] = run_EDTD_per_line( inRow, outRow, nz,
                                                  delta = ad3[0],
                                                  edges_are_zero_for_nz = edges_are_zero_for_nz )
                                                  */

    if (do_sqrt)
        for (int i=0; i<nvox; ++i) odt[i] = sqrt(odt[i]);

    return ERROR_NONE;
}

ERROR_NUMBER run_EDTD_per_line(int *roi_line, float *dist2_line, int Na,
                       float delta, bool edges_are_zero_for_nz) {
    int  idx = 0;
    int  n;
    float   *line_out;

    size_t  rowLengthInBytes = Na*sizeof(float);
    if (!(line_out=(float *)malloc(rowLengthInBytes))) return ERROR_MEMORY_ALLOCATION;

    int limit = Na-1;
    while (idx < limit){
        // get interval of line with current ROI value
        int roi = roi_line[idx];
        for (n = idx+1; n < Na; ++n){
            if (roi_line[n] != roi){
                n -= 1;
                break;
            }
        }
        // n now has the index of last matching element

        float *paddedLine=(float *)calloc(Na+2,sizeof(float));
        int start = 0, stop = limit;
        int inc=0;
        if (idx != 0 || (edges_are_zero_for_nz && roi != 0)){
            start = 1;
            inc = 1;
        }
        // put actual values from dist**2 field...
        for (int m=idx; m<=n; ++m){
            paddedLine[inc++] = dist2_line[m];
        }
        stop = inc-1;
        // pad at end?
        if (n < limit || (edges_are_zero_for_nz && roi != 0)){
            stop += 1;
        }

        // float *Df = Euclidean_DT_delta(paddedLine, stop-start+1, delta);
        float *Df = Euclidean_DT_delta(paddedLine, inc+1, delta);

        // memcpy(&(line_out[idx]), &(Df[start]), (stop-start+1)*sizeof(float));
        memcpy(&(line_out[idx]), &(Df[start]), (stop-start+1)*sizeof(float));

        free(paddedLine);
        free(Df);

        idx = n+1;
    }

    memcpy(dist2_line, line_out, rowLengthInBytes);
    free(line_out);

    return ERROR_NONE;
}

float * Euclidean_DT_delta(float *f, int n, float delta){
    /*Classical Euclidean Distance Transform (EDT) of Felzenszwalb and
        Huttenlocher (2012), but for given voxel lengths.

    Assumes that: len(f) < sqrt(10**10).

    In this version, all voxels should have equal length, and units
    are "edge length" or "number of voxels."

    Parameters
    ----------

    f     : 1D array or list, distance**2 values (or, to start, binarized
    between 0 and BIG).

    delta : voxel edge length size along a particular direction

    */

    int *v, k = 0;
    float *z, *Df;

    if (!(v=(int *)calloc(n, sizeof(int))) ||
        !(z=(float *)calloc(n+1, sizeof(float)))){
            if (v) free(v);
            return NULL;
    }
    z[0] = -BIG;
    z[1] =  BIG;

    for (int q = 1; q<n; ++q) {
        float s = f[q] + pow(q*delta, 2.0) - (f[v[k]] + pow(v[k]*delta,2.0));
        s/= 2. * delta * (q - v[k]);
        while (s <= z[k]){
            k-= 1;
            s = f[q] + pow(q*delta,2.0) - (f[v[k]] + pow(v[k]*delta, 2.0));
            s/= 2. * delta * (q - v[k]);
        }
        k+= 1;
        v[k]   = q;
        z[k]   = s;
        z[k+1] = BIG;
    }

    k   = 0;
    if (!(Df=(float *)calloc(n, sizeof(float)))){
        free(v);
        free(z);
        return NULL;
    }
    for (int q=0; q<n; ++q){
        while (z[k+1] < q * delta) k+= 1;
        Df[q] = pow(delta*(q - v[k]), 2.0) + f[v[k]];
    }

    free(v);
    free(z);

    return Df;
}

ERROR_NUMBER processIndex(int index, int *inputImg, float **outImg, THD_3dim_dataset *din){
    // Get dimensions in voxels
    int nz = DSET_NZ(din);
    int ny = DSET_NY(din);
    int nx = DSET_NX(din);
    int nvox = nx*ny*nz;

	int nVol = 1;
	int nvox3D = nx * ny * MAX(nz, 1);
	nVol = nvox / nvox3D;
	if ((nvox3D * nVol) != nvox) return ERROR_DIFFERENT_DIMENSIONS;

    // Get real world voxel sizes
    float xDim = fabs(DSET_DX(din));
    float yDim = fabs(DSET_DY(din));
    float zDim = fabs(DSET_DZ(din));

    float yDimSqrd = yDim*yDim;
    float zDimSqrd = zDim*zDim;

    if (!(*outImg=(float *)calloc(nvox, sizeof(float)))) return ERROR_MEMORY_ALLOCATION;

	for (size_t i = 0; i < nvox; i++ ) {
		if (inputImg[i] == index)
			(*outImg)[i] = BIG;
	}
	size_t nRow = ny*nz;

	//EDT in left-right direction
	for (int r = 0; r < nRow; r++ ) {
		flt * imgRow = (*outImg) + (r * nx);
		// edt1_local(din, imgRow, nx);
		edt_local(xDim, imgRow, nx);
	}

	//EDT in anterior-posterior direction
	nRow = nx * nz; //transpose XYZ to YXZ and blur Y columns with XZ Rows
	for (int v = 0; v < nVol; v++ ) { //transpose each volume separately
		flt * img3D = (flt *)calloc(nvox3D*sizeof(flt), 64); //alloc for each volume to allow openmp

		//transpose data
		size_t vo = v * nvox3D; //volume offset
		for (int z = 0; z < nz; z++ ) {
			int zo = z * nx * ny;
			for (int y = 0; y < ny; y++ ) {
				int xo = 0;
				for (int x = 0; x < nx; x++ ) {
					img3D[zo+xo+y] = (*outImg)[vo]/yDimSqrd;
					vo += 1;
					xo += ny;
				}
			}
		}
		//perform EDT for all rows
		for (int r = 0; r < nRow; r++ ) {
			flt * imgRow = img3D + (r * ny);
			edt_local(yDim, imgRow, ny);
		}
		//transpose data back
		vo = v * nvox3D; //volume offset
		for (int z = 0; z < nz; z++ ) {
			int zo = z * nx * ny;
			for (int y = 0; y < ny; y++ ) {
				int xo = 0;
				for (int x = 0; x < nx; x++ ) {
					(*outImg)[vo] = img3D[zo+xo+y];
					vo += 1;
					xo += ny;
				}
			}
		}
		free (img3D);
	} //for each volume

	//EDT in head-foot direction
	nRow = nx * ny; //transpose XYZ to ZXY and blur Z columns with XY Rows
	for (int v = 0; v < nVol; v++ ) { //transpose each volume separately
		flt * img3D = (flt *)calloc(nvox3D*sizeof(flt), 64); //alloc for each volume to allow openmp
		//transpose data
		size_t vo = v * nvox3D; //volume offset
		for (int z = 0; z < nz; z++ ) {
			for (int y = 0; y < ny; y++ ) {
				int yo = y * nz * nx;
				int xo = 0;
				for (int x = 0; x < nx; x++ ) {
					img3D[z+xo+yo] = (*outImg)[vo]/zDimSqrd;
					vo += 1;
					xo += nz;
				}
			}
		}
		//perform EDT for all "rows"
		for (int r = 0; r < nRow; r++ ) {
			flt * imgRow = img3D + (r * nz);
			edt_local(zDim, imgRow, nz);
		}
		//transpose data back
		vo = v * nvox3D; //volume offset
		for (int z = 0; z < nz; z++ ) {
			for (int y = 0; y < ny; y++ ) {
				int yo = y * nz * nx;
				int xo = 0;
				for (int x = 0; x < nx; x++ ) {
					(*outImg)[vo] = img3D[z+xo+yo];
					vo += 1;
					xo += nz;
				} //x
			} //y
		} //z
		free (img3D);
	} //for each volume

	return ERROR_NONE;
}

ERROR_NUMBER getNonzeroIndices(int nvox, int *inputImg, int *numIndices, int **indices){
    int *buffer;
    int i, j, voxelValue;
    bool    old;

    // Initialize
    *numIndices = 0;
    if (!(buffer=(int *)calloc(nvox,sizeof(int)))) return ERROR_MEMORY_ALLOCATION;

    // Count unique indices and fill buffer with set of indices
    for (i=0; i<nvox; ++i) if ((voxelValue=inputImg[i])>0){
        old = false;
        // for (j=0; j<*numIndices; ++j) if ((*indices)[j]==voxelValue) old = true;    // Core dump
        for (j=0; j<*numIndices; ++j) if (buffer[j]==voxelValue) old = true;
        if (!old){
            buffer[(*numIndices)++]=voxelValue;
        }
    }

    // Trim output buffer to number of indices
    if (!(*indices=(int *)malloc(*numIndices*sizeof(int)))){
        free(buffer);
        return ERROR_MEMORY_ALLOCATION;
    }
    for (i=0; i<*numIndices; ++i) (*indices)[i] = buffer[i];
    free(buffer);

    return ERROR_NONE;
}

int transposeYZ(float *volume, int nx, int ny, int nz){
	float * buffer;
	int nvox=nx*ny*nz;

	if (!(buffer = (float *)malloc(nvox*sizeof(float)))!=ERROR_NONE) return ERROR_MEMORY_ALLOCATION;

	int z0 = 0;
	for (int z=0; z<nz; ++z){
        int Y = z0;
        for (int y=0; y<ny; ++y){
            for (int x=0; x<nx; ++x){
                buffer[z0+x]=volume[Y];
                buffer[Y++]=volume[z0+x];
            }
        }
        z0+=nx*ny;
    }
	memcpy((void *)volume, (void *)buffer, nvox*sizeof(float));
	free(buffer);

    return ERROR_NONE;
}

int testTransposeFunction(THD_3dim_dataset * din){
    // Get dimensions in voxels
    int nz = DSET_NZ(din);
    int ny = DSET_NY(din);
    int nx = DSET_NX(din);
    int nvox = nx*ny*nz;
    int i, errorNumber;
    float *buffer;

    // Allocate memory to buffer
    if (!(buffer = (float *)malloc(nvox*sizeof(float)))) return ERROR_MEMORY_ALLOCATION;

    // Read image data
    BYTE * img = DSET_ARRAY(din, 0);

    for (i=0; i<nvox; ++i) buffer[i]=img[i];

    // Apply transpose
    if ((errorNumber=transposeYZ(buffer, nx, ny, nz))!=ERROR_NONE){
        free(buffer);
        return errorNumber;
    }

    // Output transpoed volume to afni dataset
    outputTransposedDataSet(buffer, din, nx, nz, ny);

    free(buffer);
    return ERROR_NONE;
}


int outputTransposedDataSet(float *buffer, THD_3dim_dataset *din, int nx, int nz, int ny){
    char *prefix=DSET_PREFIX(din);
    char *searchPath=DSET_DIRNAME(din);
    char *outputFileName;
    char  appendage[]="Transpose";

    // Allocate memory to output name buffer
    if (!(outputFileName=(char *)malloc(strlen(searchPath)+strlen(prefix)+strlen(appendage)+8))){
       return ERROR_MEMORY_ALLOCATION;
    }

    // Determine whether output file already exists
    int outputFileExists = doesFileExist(searchPath,prefix,appendage,outputFileName);

    // Set output dimensions
    THD_ivec3 nxyz[3]={nx, ny, nz};

    // Output Fourier spectrum image (if it does not already exist)
    if (!outputFileExists){
        THD_3dim_dataset *dout = EDIT_empty_copy(din);
        sprintf(outputFileName,"%s%s%s",searchPath,prefix,appendage);
        EDIT_dset_items( dout ,
                        ADN_prefix, outputFileName,
                        ADN_type, MRI_float,
                        ADN_nxyz, nxyz,
                        ADN_none ) ;
        EDIT_substitute_brick(dout, 0, MRI_float, buffer);
        DSET_write(dout);
    }

    // Cleanup
    free(outputFileName);

    return ERROR_NONE;
}

void edt1_local(THD_3dim_dataset * din, flt * df, int n) { //first dimension is simple

    // Get real world voxel sizes
    float xDim = fabs(DSET_DX(din));
    float yDim = fabs(DSET_DY(din));

    xDim=yDim/xDim;
    yDim=1.0f;

	int q, prevX;
	flt prevY, v;
	prevX = 0;
	prevY = BIG;
	//forward
	for (q = 0; q < n; q++ ) {
		if (df[q] == 0) {
			prevX = q;
			prevY = 0;
		} else
			df[q] = sqr((q-prevX)/xDim)+(prevY/yDim);
	}
	//reverse
	prevX = n;
	prevY = BIG;
	for (q = (n-1); q >= 0; q-- ) {
		v = sqr((q-prevX)/xDim)+(prevY/yDim);
		if (df[q] < v) {
        	prevX = q;      // DO NOT CHANGE
        	prevY = df[q];  // DO NOT CHANGE
    	} else
        	df[q] = v;
    }
}

static flt vx(flt * f, int p, int q) {
	if ((f[p] == BIG) || (f[q] == BIG))
		return BIG;
	else
		return ((f[q] + q*q) - (f[p] + p*p)) / (2.0*q - 2.0*p);
}

void edt_local(float scale, flt * f, int n) {
    float scaleSqrd = scale*scale;

	int q, p, k;
	flt s, dx;
	flt * d = (flt *)calloc((n)*sizeof(flt), 64);
	flt * z = (flt *)calloc((n)*sizeof(flt), 64);
	int * v = (int *)calloc((n)*sizeof(int), 64);

    /*# Find the lower envelope of a sequence of parabolas.
    #   f...source data (returns the Y of the parabola vertex at X)
    #   d...destination data (final distance values are written here)
    #   z...temporary used to store X coords of parabola intersections
    #   v...temporary used to store X coords of parabola vertices
    #   i...resulting X coords of parabola vertices
    #   n...number of pixels in "f" to process
    # Always add the first pixel to the enveloping set since it is
    # obviously lower than all parabolas processed so far.*/
    k = 0;
    v[0] = 0;
    z[0] = -BIG;
    z[1] = BIG;
        /*
   for q in range(1, n):
        s = ((f[q] + (q*delta)**2) - (f[v[k]] + (v[k]*delta)**2))
        s/= 2. * delta * (q - v[k])
        while s <= z[k] :
            k-= 1
            s = ((f[q] + (q*delta)**2) - (f[v[k]] + (v[k]*delta)**2))
            s/= 2. * delta * (q - v[k])
        k+= 1
        v[k]   = q
        z[k]   = s
        z[k+1] = BIG * delta
*/
/*
    for (q = 1; q < n; q++ ) {
	    /* If the new parabola is lower than the right-most parabola in
        # the envelope, remove it from the envelope. To make this
        # determination, find the X coordinate of the intersection (s)
        # between the parabolas with vertices at (q,f[q]) and (p,f[p]).*//*
        s = (f[q] + pow(q*scale,2.0)) - (f[v[k]] + pow(v[k]*scale, 2.0));
        s/= 2. * scale * (q - v[k]);
        while (s <= z[k]) {
            k-= 1;
            s = (f[q] + pow(q*scale,2.0)) - (f[v[k]] + pow(v[k]*scale,2.0));
            s/= 2.0 * scale * (q - v[k]);
        k+= 1;
        v[k]   = q;
        z[k]   = s;
        z[k+1] = BIG;
        }
    }
*/
    for (q = 1; q < n; q++ ) {
	    /* If the new parabola is lower than the right-most parabola in
        # the envelope, remove it from the envelope. To make this
        # determination, find the X coordinate of the intersection (s)
        # between the parabolas with vertices at (q,f[q]) and (p,f[p]).*/
        p = v[k]; // DO NOT CHANGE
        s = vx(f, p,q);
        while (s <= z[k]) {
            k = k - 1;
            p = v[k];       // DO NOT CHANGE
            s = vx(f, p,q); // DO NOT CHANGE
        }
        //# Add the new parabola to the envelope.
        k = k + 1;
        v[k] = q;           // DO NOT CHANGE
        z[k] = s;
        z[k + 1] = BIG;
    }
    /*# Go back through the parabolas in the envelope and evaluate them
    # in order to populate the distance values at each X coordinate.*/
    k = 0;
    for (q = 0; q < n; q++ ) {
	    while (z[k + 1] < q)
            k = k + 1;
        dx = (q - v[k])*scale;
        // d[q] = dx * dx + f[v[k]];
        d[q] = dx * dx + f[v[k]]*scaleSqrd;
    }
    for (q = 0; q < n; q++ )
		f[q] = d[q];
	free (d);
	free (z);
	free (v);
}

int doesFileExist(char * searchPath, char * prefix,char *appendage , char * outputFileName){
    int outputFileExists=0;

    struct dirent *dir;
    DIR *d;
    d = opendir(searchPath);
    sprintf(outputFileName,"%s%s",prefix,appendage);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
          if (strstr(dir->d_name,outputFileName)){
            outputFileExists=1;
            break;
          }
        }
        closedir(d);
    }

    return outputFileExists;
}

int open_input_dset(THD_3dim_dataset ** din, char * fname)
{
   *din = THD_open_dataset(fname);
   if( ! *din ) {
      fprintf(stderr,"** failed to read input dataset '%s'\n", fname);
      return ERROR_READING_FILE;
   }

   return ERROR_NONE;
}

int shortToByte(THD_3dim_dataset ** din){
    BYTE * outImg;
    int     i;

    // Get dimensions in voxels
    int nvox = DSET_NVOX(*din);

    if (!(outImg = (BYTE *)calloc(nvox, sizeof(BYTE)))){
        return ERROR_MEMORY_ALLOCATION;
    }

    // Get input data
    int brickType=DSET_BRICK_TYPE(*din, 0);
    if( brickType != MRI_short ) return ERROR_DATATYPENOTHANDLED;
    DSET_load(*din);
    short * img = (short *)DSET_ARRAY(*din, 0);

    // Map short input into BYTE output
    for (i=0; i<nvox; ++i) outImg[i]+=(img[i]!=0);

    THD_3dim_dataset *dout = EDIT_empty_copy(*din);
    char *prefix=DSET_PREFIX(*din);
    char *searchPath=DSET_DIRNAME(*din);
    EDIT_dset_items( dout ,
                    ADN_prefix, strcat(searchPath, prefix),
                    ADN_type, MRI_byte,
                    ADN_none ) ;
    EDIT_substitute_brick(dout, 0, MRI_byte, outImg);

    // Cleanup (data set)
    DSET_delete(*din);
    *din = dout;

    return ERROR_NONE;
}

int Cleanup(char *inputFileName, THD_3dim_dataset *din){

    if (inputFileName) free(inputFileName);

    return 0;
}
