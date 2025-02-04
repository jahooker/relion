/***************************************************************************
 *
 * Author: "Sjors H.W. Scheres", "Takanori Nakane"
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
/***************************************************************************
 *
 * Authors: Sjors H.W. Scheres (scheres@cnb.csic.es)
 *
 * Unidad de  Bioinformatica of Centro Nacional de Biotecnologia , CSIC
 *
 * Part of this module has been developed by Lorenzo Zampighi and Nelson Tang
 * Dept. Physiology of the David Geffen School of Medicine
 * Univ. of California, Los Angeles.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 *
 * All comments concerning this program package may be sent to the
 * e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#ifndef IMAGE_H
#define IMAGE_H

#include <memory>
#include <typeinfo>
#include <typeindex>
#include <fcntl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "src/funcs.h"
#include "src/memory.h"
#include "src/filename.h"
#include "src/multidim_array.h"
#include "src/multidim_array_statistics.h"
#include "src/transformations.h"
#include "src/metadata_table.h"
#include "src/fftw.h"
#include "src/page_operations.h"
#include "src/tiffio.h"

/// @defgroup Images Images
//@{

// An ad-hoc representation of the type system
// Allows for image data type inspection/manipulation at run time
enum DataType {
    Unknown_Type,   // Undefined data type
    UChar,          // Unsigned character or byte type (unsigned char)
    SChar,          // Signed character (for CCP4) (signed char)
    UShort,         // Unsigned integer (2-byte) (unsigned short)
    Short,          // Signed integer (2-byte) (short)
    UInt,           // Unsigned integer (4-byte) (unsigned int)
    Int,            // Signed integer (4-byte) (int)
    Long,           // Signed integer (4 or 8 byte, depending on system) (long)
    Float,          // Floating point (4-byte) (float)
    Double,         // Double precision floating point (8-byte) (RFLOAT)
    Boolean,        // Boolean (1-byte?) (bool)
    UHalf,          // Signed 4-bit integer (SerialEM extension)
};

namespace RTTI {

    static std::type_index index(DataType datatype) {
        switch (datatype) {
            case Unknown_Type: return std::type_index(typeid(void));
            case UChar:        return std::type_index(typeid(unsigned char));
            case SChar:        return std::type_index(typeid(signed char));
            case UShort:       return std::type_index(typeid(unsigned short));
            case Short:        return std::type_index(typeid(short));  // short is signed by default
            case UInt:         return std::type_index(typeid(unsigned int));
            case Int:          return std::type_index(typeid(int));  // int is signed by default
            case Long:         return std::type_index(typeid(long));
            case Float:        return std::type_index(typeid(float));
            case Double:       return std::type_index(typeid(double));
            case Boolean:      return std::type_index(typeid(bool));
            case UHalf:        return std::type_index(typeid(uhalf_t));
            default:           return std::type_index(typeid(void));
        }
    }

    // The in-memory size of datatype
    static size_t size(DataType datatype) {
        switch (datatype) {
            case Unknown_Type: return 0;
            case UChar:        return sizeof(unsigned char);
            case SChar:        return sizeof(signed char);
            case UShort:       return sizeof(unsigned short);
            case Short:        return sizeof(short);
            case UInt:         return sizeof(unsigned int);
            case Int:          return sizeof(int);
            case Long:         return sizeof(long);
            case Float:        return sizeof(float);
            case Double:       return sizeof(double);
            case Boolean:      return sizeof(bool);
            case UHalf:        throw "size_t cannot represent fractions of bytes!";
            default:           return 0;
        }
    }

};

// Convert string to int corresponding to value in enum
// int DataType::String2Int(std::string s);

// Check file datatype is same as T type to use mmap.
template <typename T>
static bool checkMmap(std::type_index u) {
    if (u == std::type_index(typeid(void)))
        REPORT_ERROR("ERROR: unknown datatype");
    if (u == std::type_index(typeid(uhalf_t)))
        return false;
    return u == std::type_index(typeid(T));
    // u must be std::type_index(typeid(U))
    // where U is one of:
    // - signed/unsigned char
    // - signed/unsigned int
    // - signed/unsigned short
    // - long
    // - float
    // - double (RFLOAT)
    // Otherwise:
    // std::cerr << "Datatype= " << u.name << std::endl;
    // REPORT_ERROR(" ERROR: cannot cast datatype to T");
}

/** WriteMode
 * To indicate the writing behavior.
 */
enum WriteMode {
    WRITE_OVERWRITE, // Forget about the old file and overwrite it
    WRITE_APPEND,	 // Append and object at the end of a stack, so far can not append stacks
    WRITE_REPLACE,	 // Replace a particular object with another
    WRITE_READONLY	 // Read-only
};


static std::string writemode2string(WriteMode mode, bool exist) {

    switch (mode) {

        case WRITE_READONLY:
        return "r";

        case WRITE_OVERWRITE:
        return "w";

        case WRITE_APPEND:
        return exist ? "r+" : "w+";  // w+ will destroy file contents. We don't want that.

        case WRITE_REPLACE:
        return "r+";

        default:
        throw "Invalid write mode!";

    }

}

/** File handler class
 * This struct is used to share the File handlers with Image Collection class
 */
class fImageHandler {

    public:

    FILE     *fimg;	// Image File handler
    FILE     *fhed;	// Image File header handler
    TIFF     *ftiff;
    FileName  ext_name; // Filename extension
    bool	  exist;    // Does the file exist?

    // Empty constructor
    fImageHandler(): fimg(nullptr), fhed(nullptr), ftiff(nullptr), ext_name(), exist(false) {}

    ~fImageHandler() { closeFile(); }

    void openFile(const FileName &name, int mode = WRITE_READONLY) {

        // Close any file that was left open in this handler
        if (fimg || fhed) closeFile();

        FileName fileName, headName = "";
        // get the format, checking for possible format specifier before suffix
        // getFileFormat("file.spi") will return "spi"
        // getFileFormat("file.spi:mrc") will return "mrc"
        // getFileFormat("file") will return ""
        ext_name = name.getFileFormat();

        long int dump;
        name.decompose(dump, fileName);
        // Subtract 1 to have numbering 0...N-1 instead of 1...N
        if (dump > 0) { dump--; }

        // create the filename from a possible input format specifier (file.spi:mrc means "it's called .spi, but it's really a .mrc")
        // file.spi:mrc -> file.spi
        fileName = fileName.removeFileFormat();

        size_t found = fileName.find_first_of("%");
        if (found != std::string::npos) { fileName = fileName.substr(0, found); }

        exist = exists(fileName);

        if (mode == WRITE_READONLY and !exist)
        REPORT_ERROR((std::string) "Can't read file " + fileName + ". It doesn't exist!");

        std::string wmstr = writemode2string((WriteMode) mode, exist); // Write mode string

        if (ext_name.contains("img") || ext_name.contains("hed")) {
            fileName = fileName.withoutExtension();
            headName = fileName.addExtension("hed");
            fileName = fileName.addExtension("img");
        } else if (ext_name.empty()) {
            ext_name = "spi"; // SPIDER is default format
            fileName = fileName.addExtension(ext_name);
        }

        bool isTiff = ext_name.contains("tif");
        if (isTiff && mode != WRITE_READONLY)
            REPORT_ERROR((std::string) "TIFF is supported only for reading");

        // Open image file
        if (
             isTiff && !(ftiff = TIFFOpen(fileName.c_str(), "r")) ||
            !isTiff && !(fimg  = fopen   (fileName.c_str(), wmstr.c_str()))
        ) {
            REPORT_ERROR((std::string) "Image::" + __func__ + " cannot open: " + name);
        }

        if (!headName.empty()) {
            if (!(fhed = fopen(headName.c_str(), wmstr.c_str())))
                REPORT_ERROR((std::string) "Image::" + __func__ + " cannot open: " + headName);
        } else {
            fhed = nullptr;
        }

    }

    // Close file (if open)
    void closeFile() {
        ext_name = "";
        exist = false;

        // Check whether the file was closed already
        if (!fimg && !fhed && !ftiff) return;

        const bool isTiff = ext_name.contains("tif");
        if (isTiff && ftiff) {
            TIFFClose(ftiff);
            ftiff = nullptr;
        }

        if (!isTiff && fclose(fimg) != 0) {
            REPORT_ERROR((std::string) "Cannot close image file ");
        } else {
            fimg = nullptr;
        }

        if (fhed && fclose(fhed) != 0) {
            REPORT_ERROR((std::string) "Cannot close header file ");
        } else {
            fhed = nullptr;
        }
    }

};


struct image_mmapper {

    FileName mapFile;  // Mapped file name
    int fd;            // Mapped file handle
    size_t len;        // Size of the mapped file
    size_t offset;

    image_mmapper(): mapFile(""), fd(0), len(0), offset(0) {}

    void* allocate(size_t size, size_t offset) {
        this->offset = offset;
        // fd = open(mapFile.c_str(), O_RDWR, S_IREAD | S_IWRITE);
        fd = open(mapFile.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1)
            REPORT_ERROR((std::string) "Image<T>::" + __func__ + ": Error opening the image file.");

        len = size + offset;
        void *ptr = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED)
            REPORT_ERROR((std::string) "Image<T>::" + __func__ + ": mmap of image file failed.");
        return ptr;
    }

    void deallocate(void *ptr) {
        munmap((char *) ptr - offset, len);
        close(fd);
    }

};

/** Swapping trigger.
 * Threshold file z size above which bytes are swapped.
 */
static const int SWAPTRIG = 0xffff;

// Generic image class template
template<typename T>
class Image {

    public:

    MultidimArray<T> data;  // Image data
    MetaDataTable header;   // File metadata

    // Why int x, y, z but long int n?
    struct Dimensions { int x, y, z; long int n; };

    private:

    FileName filename; // File name
    FILE *fimg;  // Image  file handle
    FILE *fhed;  // Header file handle
    bool isStack;
    long offset;  // Data offset
    long pad;     // Data pad
    bool swap;    // Swap bytes when reading
    long int replaceNsize;  // Stack size in the replace case
    bool _exists;  // Does the target file exist? 0 if file does not exist or is not a stack.

    // Allocation
    image_mmapper *mmapper;

    public:

    /** Empty constructor
     *
     * An empty image is created.
     *
     * @code
     * Image<RFLOAT> I;
     * @endcode
     */
    Image(): mmapper(nullptr) {
        clear();
        header.addObject();
    }

    Image(MultidimArray<T> arr): mmapper(nullptr) {
        clear();
        header.addObject();
        data = std::move(arr);
    }

    static Image<RFLOAT> from_filename(const FileName &fn, bool readdata = true) {
        Image<RFLOAT> img;
        img.read(fn, readdata);
        return img;
    }

    /** Constructor with size
     *
     * A blank image (0.0 filled) is created with the given size.
     *
     * @code
     * Image I(64, 64);
     * @endcode
     */
    Image(long int Xdim, long int Ydim, long int Zdim = 1, long int Ndim = 1): mmapper(nullptr) {
        clear();
        data.resize(Xdim, Ydim, Zdim, Ndim);
        header.addObject();
    }

    inline static Image<T> zeros(long int Xdim, long int Ydim, long int Zdim = 1, long int Ndim = 1) {
        Image<T> img(Xdim, Ydim, Zdim, Ndim);
        img.data.initZeros();
        return img;
    }

    void clear() {
        if (mmapper) mmapper->deallocate(data.data);
        delete mmapper;
        mmapper = nullptr;

        header.clear();
        data.clear();

        filename.clear();
        swap = false;
        offset = 0;
        replaceNsize = 0;
    }

    ~Image() { clear(); }

    // Read/write functions for different file formats

    DataType readSPIDER(long int img_select);

    int writeSPIDER(long int select_img=-1, bool isStack = false, int mode = WRITE_OVERWRITE);

    DataType readMRC(long int img_select, bool isStack = false, const FileName &name = "");

    int writeMRC(long int img_select, bool isStack = false, int mode = WRITE_OVERWRITE);

    DataType readIMAGIC(long int img_select);

    void writeIMAGIC(long int img_select = -1, int mode = WRITE_OVERWRITE);

    int readTIFF(
        TIFF *ftiff, long int img_select,
        bool readdata = false, bool isStack = false, const FileName &name = ""
    );

    /** Is this file an image?
     *
     *	Check whether a real-space image can be read.
     */
    bool isImage(const FileName &name) { return !read(name, false); }

    // Rename the image
    void rename(const FileName &name) { filename = name; }

    /** General read function
     * you can read a single image from a single image file
     * or a single image file from an stack, in the second case
     * the select slide may come in the image name or in the select_img parameter
     * file name takes precedence over select_img
     * If -1 is given the whole object is read
     * The number before @ in the filename is 1-indexed, while select_img is 0-indexed.
     */
    int read(
        const FileName &name, bool readdata = true, long int select_img = -1,
        image_mmapper *mmapper = nullptr, bool is_2D = false
    );

    /** Read from an open file
     */
    int readFromOpenFile(
        const FileName &name, fImageHandler &hFile, long int select_img,
        bool is_2D = false
    ) {
        const int err = _read(name, hFile, true, select_img, nullptr, is_2D);
        rewind(fimg);  // Reposition file pointer for a next read
        return err;
    }

    /** General write function
     * select_img = which slice should I replace
     * overwrite = 0, append slice
     * overwrite = 1 overwrite slice
     *
     * NOTE:
     *	select_img has higher priority than the number before "@" in the name.
     *	select_img counts from 0, while the number before "@" in the name from 1!
     */
    void write(
        const FileName &name = "", long int select_img = -1, bool isStack = false,
        int mode = WRITE_OVERWRITE
    );

    /** Write an entire page as datatype
     *
     * A page of datasize_n elements T is cast to datatype and written to fimg
     * The memory for the casted page is allocated and freed internally.
     */
    template <typename U>
    void writePageAsDatatype(size_t datasize_n) {
        const size_t datasize = datasize_n * sizeof(U);
        const auto deleter = [] (char *ptr) { callocator<char>::deallocate(ptr, datasize); };
        const auto page = std::unique_ptr<char, decltype(deleter)>(callocator<char>::allocate(datasize), deleter);
        castToPage(page.get(), data.data, typeid(U), datasize_n);
        fwrite(page.get(), datasize, 1, fimg);
    }

    // Read the raw data
    int readData(long int select_img, DataType datatype) {
        // #define DEBUG
        #ifdef DEBUG
        std::cerr << "entering " << __func__ << std::endl;
        #endif

        const auto index_u = RTTI::index(datatype);
        size_t size_u; // bytes
        size_t bytes_per_slice; // bytes
        if (index_u == std::type_index(typeid(uhalf_t))) {
            // Guarantee divisibility by 2
            if (Xsize(data) * Ysize(data) % 2 != 0)
                REPORT_ERROR("For UHalf, Xsize(data) * Ysize(data) must be even.");
            // size_u not assigned because half-bytes cannot be represented
            bytes_per_slice = Xsize(data) * Ysize(data) * Zsize(data) / 2;
        } else {
            size_u = RTTI::size(datatype);
            bytes_per_slice = Xsize(data) * Ysize(data) * Zsize(data) * size_u;
        }

        if (data.getMmap()) { delete mmapper; mmapper = nullptr; }

        // Check if mapping is possible
        if (mmapper && !checkMmap<T>(index_u)) {
            std::cout << "WARNING: Image Class. File datatype and image declaration not compatible with mmap. Loading into memory." << std::endl;
            delete mmapper;
            mmapper = nullptr;
        }

        if (mmapper) {
            if (Nsize(data) > 1) {
                REPORT_ERROR(
                    (std::string) "Image<T>::" + __func__ + ": mmap with multiple \
                    images file not compatible. Try selecting a unique image."
                );
            }
            fclose(fimg);
            data.data = reinterpret_cast<T*>(mmapper->allocate(bytes_per_slice, offset));
            return 0;
        } else {
            // Reset select to get the correct offset
            if (select_img < 0) { select_img = 0; }

            // Allocate memory for image data
            // (Assume xdim, ydim, zdim and ndim are already set)
            // if memory already allocated use it (no resize allowed)
            data.coreAllocate();
            // Each image occupies bytes_per_slice + pad bytes
            const size_t off = offset + select_img * (bytes_per_slice + pad);
            // #define DEBUG
            #ifdef DEBUG
            data.printShape();
            printf("DEBUG: Page size: %ld offset= %d \n", bytes_per_slice, offset);
            printf("DEBUG: Swap = %d  Pad = %ld  Offset = %ld\n", swap, pad, offset);
            printf("DEBUG: off = %d select_img= %d \n", off, select_img);
            #endif

            const int err = transcription::copyViaPage
                (data, fimg, bytes_per_slice, index_u, size_u, off, pad, swap);

            #ifdef DEBUG
            printf("DEBUG img_read_data: Finished reading and converting data\n");
            #endif

            return err;
        }
    }

    /** Data access
     *
     * This operator can be used to access the data multidimarray.
     * In this way
     * we could resize an image just by resizing its associated matrix:
     * @code
     * image().resize(128, 128);
     * @endcode
     * or we could add two images by adding their matrices.
     * @code
     * image1() = image2() + image3();
     * @endcode
     */
    MultidimArray<T>& operator () () { return data; }

    const MultidimArray<T>& operator () () const { return data; }

    /** Pixel access
    *
    * This operator is used to access a pixel within a 2D image. This is a
    * logical access, so you could access to negative positions if the image
    * has been defined so (see the general explanation for the class).
    *
    * @code
    * std::cout << "Grey level of pixel (-3,-3) of the image = " << I(-3, -3)
    * << std::endl;
    *
    * I(-3, -3) = I(-3, -2);
    * @endcode
    */
    const T& operator () (int i, int j) const {
        return data.elem(i, j);
    }

    T& operator () (int i, int j) {
        return data.elem(i, j);
    }

    #ifdef IMGPIXEL
    /** Set pixel
     * (direct access) needed by swig
     */
    void setPixel(int i, int j, T v) {
        IMGPIXEL(*this, i, j) = v;
    }

    /** Get pixel
     * (direct acces) needed by swig
     */
    T getPixel(int i, int j) const {
        return IMGPIXEL(*this, i, j);
    }
    #endif

    /** Voxel access
     *
     * This operator is used to access a voxel within a 3D image. This is a
     * logical access, so you could access to negative positions if the image
     * has been defined so (see the general explanation for the class).
     *
     * @code
     * std::cout << "Grey level of pixel (-3,-3, 1) of the volume = " << I(-3, -3, 1)
     * << std::endl;
     *
     * I(-3, -3, 1) = I(-3, -2, 0);
     * @endcode
     */
    inline const T& operator () (int k, int i, int j) const {
        return data.elem(i, j, k);
    }

    inline T& operator () (int k, int i, int j) {
        return data.elem(i, j, k);
    }

    /** const reference to filename
     *
     * @code
     * std::cout << "Image name = " << I.name() << std::endl;
     * @endcode
     */
    const FileName& name() const {
        return filename;
    }

    /** Get Image dimensions
     */
    Dimensions getDimensions() const {
        Dimensions dimensions;
        dimensions.x = Xsize(data);
        dimensions.y = Ysize(data);
        dimensions.z = Zsize(data);
        dimensions.n = Nsize(data);
        return dimensions;
    }

    long unsigned int getSize() const {
        return data.size();
    }

    /* Is there label in the main header */
    bool mainContainsLabel(EMDL::EMDLabel label) const {
        return header.containsLabel(label);
    }

    /** Data type
     *
     * @code
     * std::cout << "datatype= " << dataType() << std::endl;
     * @endcode
     */
    int dataType() const {
        return header.getValue<int>(EMDL::IMAGE_DATATYPE, header.size() - 1);
    }

    /** Sampling rate in X
    *
    * @code
    * std::cout << "sampling= " << samplingRateX() << std::endl;
    * @endcode
    */
    RFLOAT samplingRateX(const long int n = 0) const {
        if (header.containsLabel(EMDL::IMAGE_SAMPLINGRATE_X))
            return header.getValue<RFLOAT>(EMDL::IMAGE_SAMPLINGRATE_X, header.size() - 1);
        else return 1.0;
    }

    /** Sampling rate in Y
    *
    * @code
    * std::cout << "sampling= " << samplingRateY() << std::endl;
    * @endcode
    */
    RFLOAT samplingRateY(const long int n = 0) const {
        if (header.containsLabel(EMDL::IMAGE_SAMPLINGRATE_Y))
            return header.getValue<RFLOAT>(EMDL::IMAGE_SAMPLINGRATE_Y, header.size() - 1);
        else return 1.0;
    }

    // Set file name
    void setName(const FileName &filename) {
        this->filename = filename;
    }

    // Set image statistics in the main header
    void setStatisticsInHeader() {
        const auto statistics = computeStats(data);
        const long int i = header.size() - 1;
        header.setValue(EMDL::IMAGE_STATS_AVG,    statistics.avg,    i);
        header.setValue(EMDL::IMAGE_STATS_STDDEV, statistics.stddev, i);
        header.setValue(EMDL::IMAGE_STATS_MIN,    statistics.min,    i);
        header.setValue(EMDL::IMAGE_STATS_MAX,    statistics.max,    i);
    }

    void setSamplingRateInHeader(RFLOAT rate_x, RFLOAT rate_y , RFLOAT rate_z) {
        const long int i = header.size() - 1;
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_X, rate_x, i);
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_Y, rate_y, i);
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_Z, rate_z, i);
    }

    void setSamplingRateInHeader(RFLOAT rate_x, RFLOAT rate_y) {
        const long int i = header.size() - 1;
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_X, rate_x, i);
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_Y, rate_y, i);
    }

    void setSamplingRateInHeader(RFLOAT rate) {
        const long int i = header.size() - 1;
        if (Xsize(data) > 1)
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_X, rate, i);
        if (Ysize(data) > 1)
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_Y, rate, i);
        if (Zsize(data) > 1)
        header.setValue(EMDL::IMAGE_SAMPLINGRATE_Z, rate, i);
    }

    // Show image properties
    friend std::ostream& operator << (std::ostream& o, const Image<T>& I) {
        o << "Image type   : ";
        o << "Real-space image" << std::endl;

        o << "Reversed	   : ";
        o << (I.swap ? "TRUE" : "FALSE") << std::endl;

        o << "Data type    : ";
        switch (I.dataType()) {
            case Unknown_Type:
            o << "Undefined data type";
            break;

            case UChar:
            o << "Unsigned character or byte type";
            break;

            case SChar:
            o << "Signed character (for CCP4)";
            break;

            case UShort:
            o << "Unsigned integer (2-byte)";
            break;

            case Short:
            o << "Signed integer (2-byte)";
            break;

            case UInt:
            o << "Unsigned integer (4-byte)";
            break;

            case Int:
            o << "Signed integer (4-byte)";
            break;

            case Long:
            o << "Signed integer (4 or 8 byte, depending on system)";
            break;

            case Float:
            o << "Floating point (4-byte)";
            break;

            case Double:
            o << "Double precision floating point (8-byte)";
            break;

            case Boolean:
            o << "Boolean (1-byte?)";
            break;

            case UHalf:
            o << "4-bit integer";
            break;

            default:
            break;

        }
        o << std::endl;
        o << "dimensions   : " << Xsize(I()) << " x " << Ysize(I()) << " x " << Zsize(I()) << " x " << Nsize(I());
        o << "	(noObjects x slices x rows x columns)" << std::endl;
        return o;
    }

    int readTiffInMemory(
        void* buf, size_t size, bool readdata = true, long int select_img = -1,
        image_mmapper *mmapper = nullptr, bool is_2D = false
    );

    private:

    int _read(
        const FileName &name, fImageHandler &hFile, bool readdata = true, long int select_img = -1,
        image_mmapper *mmapper = nullptr, bool is_2D = false
    );

    void _write(
        const FileName &name, fImageHandler &hFile, long int select_img=-1,
        bool isStack = false, int mode = WRITE_OVERWRITE
    );

};

// Some image-specific operations

// For image normalisation
void normalise(
    Image<RFLOAT> &I,
    int bg_radius,
    RFLOAT white_dust_stddev, RFLOAT black_dust_stddev,
    bool do_ramp, bool is_helical_segment = false,
    RFLOAT helical_mask_tube_outer_radius_pix = -1.0,
    RFLOAT tilt_deg = 0.0, RFLOAT psi_deg = 0.0
);

Stats<RFLOAT> calculateBackgroundAvgStddev(
    Image<RFLOAT> &I,
    int bg_radius,
    bool is_helical_segment = false,
    RFLOAT helical_mask_tube_outer_radius_pix = -1.0,
    RFLOAT tilt_deg = 0.0, RFLOAT psi_deg = 0.0
);

void subtractBackgroundRamp(
    Image<RFLOAT> &I,
    int bg_radius,
    bool is_helical_segment = false,
    RFLOAT helical_mask_tube_outer_radius_pix = -1.0,
    RFLOAT tilt_deg = 0.0, RFLOAT psi_deg = 0.0
);

// For dust removal
void removeDust(
    Image<RFLOAT> &I, bool is_white, RFLOAT thresh,
    RFLOAT avg, RFLOAT stddev
);

// for contrast inversion
void invert_contrast(Image<RFLOAT> &I);

// for image re-scaling
void rescale(Image<RFLOAT> &I, int mysize);

// for image re-windowing
void rewindow(Image<RFLOAT> &I, int mysize);

/// @defgroup ImageFormats Image Formats
/// @ingroup Images
// Functions belonging to this topic are commented in rw*.h
//@}

std::pair<RFLOAT, RFLOAT> getImageContrast(MultidimArray<RFLOAT> &image, RFLOAT minval, RFLOAT maxval, RFLOAT &sigma_contrast);

#endif
