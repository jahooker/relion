/***************************************************************************
 *
 * Author: "Sjors H.W. Scheres"
 * MRC Laboratory of Molecular Biology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/
/*
 Based on rwIMAGIC.h
 Header file for reading and writing Image Science's Imagic files
 Format: 2D image file format for the program Imagic (Image Science)
 Author: Bernard Heymann
 Created: 19990424  Modified: 20011030
*/

#ifndef RWIMAGIC_H_
#define RWIMAGIC_H_

#include "src/metadata_label.h"
#include "src/image.h"

const int IMAGICSIZE = 1024;  // Size of the IMAGIC header for each image

///@defgroup Imagic Imagic File format
///@ingroup ImageFormats

/** Imagic Header
  * @ingroup Imagic
*/
struct IMAGIChead {             
    // file header for IMAGIC data
    int imn;          //  0      image location number (1,2,...)
    int ifn;          //  1      # images following
    int ierror;       //  2      error code: error if >0
    int nhfr;         //  3      # header records per image
    int ndate;        //  4      creation day
    int nmonth;       //  5      creation month
    int nyear;        //  6      creation year
    int nhour;        //  7      creation hour
    int nminut;       //  8      creation minute
    int nsec;         //  9      creation second
    int npix2;        // 10      # 4-byte reals in image
    int npixel;       // 11      # image elements
    int ixlp;       // 12      lines per image (Y)
    int iylp;        // 13      pixels per line (X)
    char type[4];      // 14      image type
    int ixold;       // 15      top-left X coordinate
    int iyold;       // 16      top-left Y coordinate
    float avdens;       // 17      average
    float sigma;       // 18      standard deviation
    float varian;       // 19      variance
    float oldavd;      // 20      old average
    float densmax;       // 21      maximum
    float densmin;       // 22      minimum
    //     RFLOAT sum;       // 23+24  sum of densities
    //     RFLOAT squares;    // 25+26  sum of squares
    float dummy[4];   // 23-26  dummy place holder
    char lastpr[8];      // 27+28     last program writing file
    char name[80];       // 29-48     image name
    float extra_1[8];   // 49-56     additional parameters
    float eman_alt;   // 57      EMAN: equiv to psi & PFT omega
    float eman_az;    // 58      EMAN: equiv to theta
    float eman_phi;   // 59      EMAN: equiv to phi
    float extra_2[69];   // 60-128     additional parameters
    float euler_alpha;  // 129   Euler angles: psi
    float euler_beta;  // 130       theta
    float euler_gamma;  // 131       phi
    float proj_weight;  // 132   weight of each projection
    float extra_3[66];   // 133-198     additional parameters
    char history[228];      // 199-255   history
} ;

inline DataType determine_datatype(IMAGIChead *header) throw (RelionError) {
    if (strstr(header->type, "PACK")) return UChar;
    if (strstr(header->type, "INTG")) return Short;
    if (strstr(header->type, "REAL")) return Float;
    if (strstr(header->type, "RECO") || strstr(header->type,"COMP"))
    REPORT_ERROR("readIMAGIC: only real-space images can be read into RELION");
}

/************************************************************************
@Function: readIMAGIC
@Description:
 Reading an IMAGIC image format.
@Algorithm:
 A 2D file format for the IMAGIC package.
 The header is stored in a separate file with extension ".hed" and
  a fixed size of 1024 bytes per image.
 The image data is stored in a single block in a file with the
  extension ".img".
 Byte order determination: Year and hour values
        must be less than 256*256.
 Data types:     PACK = byte, INTG = short, REAL = float,
        RECO,COMP = complex float.
 Note that the x and y dimensions are interchanged (actually a display issue).
@Arguments:
 Bimage* p   the image structure.
 int select   image selection in multi-image file (-1 = all images).
@Returns:
 int     error code (<0 means failure).
**************************************************************************/
/** Imagic reader
  * @ingroup Imagic
*/

template <typename T>
int Image<T>::readIMAGIC(long int img_select) {
    #ifdef DEBUG
    printf("DEBUG readIMAGIC: Reading Imagic file\n");
    #endif

    IMAGIChead *header = new IMAGIChead;

    if (fread(header, IMAGICSIZE, 1, fhed) < 1)
        REPORT_ERROR((std::string) "readIMAGIC: header file of " + filename + " cannot be read");

    // Determine byte order and swap bytes if from little-endian machine
    char *b = (char *) header;
    long int extent = IMAGICSIZE - 916;  // exclude char bytes from swapping
    if (abs(header->nyear) > SWAPTRIG || header->ixlp > SWAPTRIG) {
        for (int i = 0; i < extent; i += 4)
            if (i != 56)          // exclude type string
                swapbytes(b + i, 4);
    }

    std::array<unsigned long int, 4> dims { (unsigned long int) header->iylp, (unsigned long int) header->ixlp, 1, (unsigned long int) header->ifn + 1 };

    if (img_select > (long int) dims[3]) {
        REPORT_ERROR((std::string) "readImagic: Image number " + std::to_string(img_select) + " exceeds stack size " + std::to_string(dims[3]));
    }

    if (img_select > -1)
        dims[3] = 1;
    // setDimensions do not allocate data
    data.setDimensions(dims[0], dims[1], dims[2], dims[3]);
    replaceNsize = dims[3];

    DataType datatype = determine_datatype(header);

    // Set min-max values and other statistical values
    if (header->sigma == 0 && header->varian != 0) {
        header->sigma = sqrt(header->varian);
    }
    if (header->densmax == 0 && header->densmin == 0 && header->sigma != 0) {
        header->densmin = header->avdens - header->sigma;
        header->densmax = header->avdens + header->sigma;
    }

    const long int i = this->header.size() - 1;
    this->header.setValue(EMDL::IMAGE_STATS_MIN,    (RFLOAT) header->densmin, i);
    this->header.setValue(EMDL::IMAGE_STATS_MAX,    (RFLOAT) header->densmax, i);
    this->header.setValue(EMDL::IMAGE_STATS_AVG,    (RFLOAT) header->avdens, i);
    this->header.setValue(EMDL::IMAGE_STATS_STDDEV, (RFLOAT) header->sigma, i);
    setSamplingRateInHeader((RFLOAT) 1.0);
    this->header.setValue(EMDL::IMAGE_DATATYPE, (int) datatype, i);

    offset = 0;   // separate header file

    if (!dataflag) {
        // Don't read the individual header and the data if not necessary
    	delete header;
    	return 0;
    }

   // Get the header information
    int error_fseek = img_select > -1 ? fseek(fhed, img_select * IMAGICSIZE, SEEK_SET) : fseek(fhed, 0, SEEK_SET);
    if (error_fseek != 0) return -1;

    delete header;

    return readData(fimg, img_select, datatype, 0);

}

/************************************************************************
@Function: writeIMAGIC
@Description:
 Writing an IMAGIC image format.
@Algorithm:
 A file format for the IMAGIC package.
@Arguments:
 Bimage*    the image structure.
@Returns:
 int     error code (<0 means failure).
**************************************************************************/
/** Imagic Writer
  * @ingroup Imagic
*/
template <typename T>
void Image<T>::writeIMAGIC(long int img_select, int mode) {
    //    if ( p->transform != NoTransform )
    //        img_convert_fourier(p, Centered);

    IMAGIChead *header = new IMAGIChead;

    const auto dims = data.getDimensions();

    // Fill in the file header
    header->nhfr   = 1;
    header->npix2  = dims[0] * dims[1];
    header->npixel = header->npix2;
    header->iylp   = dims[0];
    header->ixlp   = dims[1];
    header->ifn    = dims[3] - 1;

    time_t timer;
    time (&timer);
    tm *t = localtime(&timer);

    header->ndate  = t->tm_mday;
    header->nmonth = t->tm_mon + 1;
    header->nyear  = t->tm_year;
    header->nhour  = t->tm_hour;
    header->nminut = t->tm_min;
    header->nsec   = t->tm_sec;

    // Convert T to datatype
    if (
        typeid(T) == typeid(RFLOAT) ||
        typeid(T) == typeid(float) ||
        typeid(T) == typeid(int)
    ) {
        strncpy(header->type, "REAL", 4);
    } else if (
        typeid(T) == typeid(unsigned char) ||
        typeid(T) == typeid(signed char)
    ) {
        strncpy(header->type, "PACK", 4);
    } else {
        REPORT_ERROR("ERROR write IMAGIC image: invalid typeid(T)");
    }

    const size_t datasize = dims[0] * dims[1] * dims[2] * gettypesize(Float);

    if (!this->header.empty()) {

        const long int i = this->header.size() - 1;

        try {
            header->densmin = this->header.template getValue<float>(EMDL::IMAGE_STATS_MIN, i);
        } catch (const char* errmsg) {}
        try {
            header->densmax = this->header.template getValue<float>(EMDL::IMAGE_STATS_MAX, i);
        } catch (const char* errmsg) {}
        try {
            header->avdens = this->header.template getValue<float>(EMDL::IMAGE_STATS_AVG, i);
        } catch (const char* errmsg) {}
        try {
            const float sigma = this->header.template getValue<float>(EMDL::IMAGE_STATS_STDDEV, i);
            header->sigma = sigma;
            header->varian = sigma * sigma;
        } catch (const char* errmsg) {}
    }

    memcpy(header->lastpr, "Xmipp", 5);
    memcpy(header->name, filename.c_str(), 80);

    /*
     * BLOCK HEADER IF NEEDED
     */
    struct flock fl;

    fl.l_type   = F_WRLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK    */
    fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
    fl.l_start  = 0;        /* Offset from l_whence         */
    fl.l_len    = 0;        /* length, 0 = to EOF           */
    fl.l_pid    = getpid(); /* our PID                      */
    fcntl(fileno(fimg), F_SETLKW, &fl); /* locked */
    fcntl(fileno(fhed), F_SETLKW, &fl); /* locked */

    if (mode == WRITE_APPEND) {
        fseek(fimg, 0, SEEK_END);
        fseek(fhed, 0, SEEK_END);
    } else if (mode == WRITE_REPLACE) {
        fseek(fimg, datasize   * img_select, SEEK_SET);
        fseek(fhed, IMAGICSIZE * img_select, SEEK_SET);
    } else /* if (mode == WRITE_OVERWRITE) */ {
        fseek(fimg, 0, SEEK_SET);
        fseek(fhed, 0, SEEK_SET);
    }
    char* fdata = (char*) askMemory(datasize);

    // Unlock
    fl.l_type   = F_UNLCK;
    fcntl(fileno(fimg), F_SETLK, &fl); /* unlocked */
    fcntl(fileno(fhed), F_SETLK, &fl); /* unlocked */

    freeMemory(fdata, datasize);

    delete header;
}

#endif /* RWIMAGIC_H_ */
