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
/***************************************************************************
 *
 * Authors:     Carlos Oscar S. Sorzano (coss@cnb.csic.es)
 *
 * Unidad de  Bioinformatica of Centro Nacional de Biotecnologia , CSIC
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
 *  All comments concerning this program package may be sent to the
 *  e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#ifndef MACROS_H
#define MACROS_H

#define RELION_SHORT_VERSION "3.1.3"
extern const char *g_RELION_VERSION;

#include <math.h>
#include <signal.h>
#include "src/pipeline_control.h"
#include "src/error.h"

#ifndef _CYGWIN
    #ifdef __APPLE__
        #include <limits.h>
    #else
        #include <values.h>
    #endif
#endif

#ifndef MINFLOAT
    #define MINFLOAT -1e30
#endif
#ifndef MAXFLOAT
    #define MAXFLOAT  1e30
#endif

#ifdef RELION_SINGLE_PRECISION
    typedef float RFLOAT;
    #define LARGE_NUMBER 99e36
#else
    typedef double RFLOAT;
    #define LARGE_NUMBER 99e99
#endif

#if defined CUDA and DEBUG_CUDA
    #define CRITICAL(string) raise(SIGSEGV);
#else
    #define CRITICAL(string) REPORT_ERROR(string);
#endif

//#define DEBUG
//#define DEBUG_CHECKSIZES

/// @defgroup Macros Macros
/// @ingroup DataLibrary
//@{
/// @name Constants
//@{

/** π
 * @ingroup MacrosConstants
 */
#ifndef PI
constexpr double PI = 3.14159265358979323846;
#endif

//@}

/// @name Numerical functions
//@{

inline int make_even(int n) {
    return n - n % 2;  // n ^ 1;
}

inline float sinc(float theta) {
    return sin(theta) / theta;
}

inline double sinc(double theta) {
    return sin(theta) / theta;
}

/** signum
 *
 * The sign of a value.
 * Valid for any type T which supports
 * - total ordering via operator <
 * - and construction from 0.
 * If n is positive, sgn(n) returns +1.
 * If n is negative, sgn(n) returns -1.
 * If n is 0,        sgn(n) returns  0.
 * (where 0, +1, -1 are of type T)
 *
 * @code
 * if (sgn(x) == -1) { std::cout << "x is negative" << std::endl; }
 * @endcode
 */
template <typename T>
inline int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

template <typename T>
inline int sgn_nozero(T val) {
    return val >= T(0) ? +1 : -1;
}

// Part of namespace std since C++17
inline float hypot(float a, float b, float c) {
    return sqrt(a * a + b * b + c * c);
}

inline double hypot(double a, double b, double c) {
    return sqrt(a * a + b * b + c * c);
}

inline long double hypot(long double a, long double b, long double c) {
    return sqrt(a * a + b * b + c * c);
}

template <typename T>
inline T hypot2(T a, T b) { return a * a + b * b; }

template <typename T>
inline T hypot2(T a, T b, T c) { return a * a + b * b + c * c; }

// std::clamp from C++17
template <typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
    return v < lo ? lo : hi < v ? hi : v;
}

/** Wrapping for integers
 *
 * wrap uses modular arithmetic to "wrap" the integers into a cycle between x0 and xF.
 *
 * For example the following code:
 * @code
 * for (int x = -4; x <= +4; x++) {
 *     std::cout << "(" << x << " " << wrap(x, -2, +2) << ") ";
 * }
 * std::cout << std::endl;
 * @endcode
 * will output
 * @code
 * (-4 1) (-3 2) (-2 -2) (-1 -1) (0 0) (1 1) (2 2) (3 -2) (4 -1)
 * @endcode
 *
 * wrap replaces a macro intWRAP
 * @code
 * #define intWRAP(x, x0, xF) (((x) >= (x0) && (x) <= (xF)) ? (x) : ((x) < (x0)) ? ((x) - (int)(((x) - (x0) + 1) / ((xF) - (x0) + 1) - 1) *  ((xF) - (x0) + 1)) : ((x) - (int)(((x) - (xF) - 1) / ((xF) - (x0) + 1) + 1) * ((xF) - (x0) + 1)))
 * @endcode
 */
inline int wrap(int x, int x0, int xF) {
    int base = xF - x0 + 1;
    int modulus = x % base;
    if (modulus < x0) return modulus + base;
    if (modulus > xF) return modulus - base;
    return modulus;
}

/** Wrapping for real numbers
 *
 * wrap is used to keep a real number within the interval [x0, xF].
 * It does this by "wrapping" the reals into a cycle.
 * This is useful when dealing with angles,
 * which we wish to constrain to the interval [0, 2 * PI].
 * For instance, 5 * PI ought to be the same as PI.
 *
 * @code
 * wrap(5 * PI, 0, 2 * PI); // returns PI (or close enough)
 * @endcode
 * 
 * This replaces a macro realWRAP:
 * @code
 * #define realWRAP(x, x0, xF) ((x0) <= (x) && (x) <= (xF) ? \
 *     (x) : ((x) < (x0)) ? \
 *     ((x) - (int)(((x) - (x0)) / ((xF) - (x0)) - 1) * ((xF) - (x0))) : ((x) - (int)(((x) - (xF)) / ((xF) - (x0)) + 1) * ((xF) - (x0))))
 * @endcode
 *
 */
inline RFLOAT wrap(RFLOAT x, RFLOAT x0, RFLOAT xF) {
    RFLOAT range = xF - x0;
    if (x < x0) return x + range * (int) (1 + (x0 - x) / range);
    if (x > xF) return x - range * (int) (1 + (x - xF) / range);
    return x;
}

/** Degrees to radians
 *
 * @code
 * phi = radians(30.0); // 0.5235987755982988
 * @endcode
 */
constexpr inline float  radians(float  theta) { return theta * (float)  PI / 180.f; }
constexpr inline double radians(double theta) { return theta * (double) PI / 180.0; }

/** Radians to degrees
 *
 * @code
 * degrees(PI / 6.0); // 30.0
 * @endcode
 */
constexpr inline float  degrees(float  theta) { return theta * 180.f / (float)  PI; }
constexpr inline double degrees(double theta) { return theta * 180.0 / (double) PI; }

/** SINC function
 *
 * The sinc function is defined as sin(PI * x) / (PI * x).
 */
#define SINC(x) (((x) < 0.0001 && (x) > -0.0001) ? 1 : sin(PI * (x)) / (PI * (x)))

#if defined HAVE_SINCOS || defined DOXGEN

    /** Sincos function
     *
     *  Wrappper to make sincos(x, &sinx, &cosx) work for all compilers.
     */
    #define SINCOS(x, s, c) sincos(x, s, c)

    /** Sincosf function
     *
     *  Wrappper to make sincosf(x, &sinx, &cosx) work for all compilers.
     */
    #define SINCOSF(x, s, c) sincosf(x, s, c)

#elif defined HAVE___SINCOS
    // Use __sincos and __sincosf instead (primarily clang)

        #define SINCOS (x, s, c) __sincos (x, s, c)
        #define SINCOSF(x, s, c) __sincosf(x, s, c)

#else
    // Neither sincos or __sincos available, use raw functions.

        static void SINCOS(double x, double *s, double *c) {
            *s = sin(x);
            *c = cos(x);
        }
        static void SINCOSF(float x, float *s, float *c) {
            *s = sinf(x);
            *c = cosf(x);
        }

#endif

/** Returns next positive power_class of 2
 *
 * x is supposed to be positive, but it needn't be an integer.
 *
 * @code
 * next_power = NEXT_POWER_OF_2(1000); // next_power = 1024
 * @endcode
 */
#define NEXT_POWER_OF_2(x) (1 << ceil(log((RFLOAT) x) / log(2.0) - Xmipp::epsilon))

/** Linear interpolation
 *
 * When x = 0 => x0
 * When x = 1 => xF
 * When x = 0.5 => 0.5 * (x0 + xF)
 */
template <typename T>
inline T LIN_INTERP(RFLOAT x, T x0, T xF) {
    return x0 + x * (xF - x0);
    // lerp(x0, xF, x);  // C++20
    // (x * xF) + (1 - x) * x0
}

/// @name Miscellaneous
//@{

namespace Xmipp {

    /** (XMIPP_EQUAL_ACCURACY)
     *
     * An epsilon for determining whether two values should be considered equal.
     * Value depends on whether RELION is operating in single or in double precision.
     * Required to correctly find symmetry subgroups.
     */
    template <typename T> constexpr T epsilon();

    template<> constexpr float epsilon() {
        return 1e-4;
    }

    template<> constexpr double epsilon() {
        return 1e-6;
    }

    // The first index of an Xmipp volume/image/array of size 'size'.
    constexpr inline long int init(long int size) {
        return -(size / 2);
    }

    // The last index of an Xmipp volume/image/array of size 'size'.
    constexpr inline long int last(long int size) {
        return size - (size / 2) - 1;
        // n / 2 + n % 2 - 1
    }

    // lt and gt are more stringent than ordinary > and <,
    // to allow for a more relaxed definition of equality.
    // lt(x, y) can be false even if x < y is true
    // and gt(x, y) can be false even if x > y is true
    // This is to ignore insignificant differences caused by machine error.

    // Less than
    template <typename T>
    constexpr inline T lt(T x, T y) {
        return x < y - epsilon<T>();
    }

    // Greater than
    template <typename T>
    constexpr inline T gt(T x, T y) {
        return x > y + epsilon<T>();
    }

    // Equality
    template <typename T>
    constexpr inline bool eq(T x, T y) {
        return abs(x - y) < epsilon;
    }

}

static void PRINT_VERSION_INFO() {

    std::cout << "RELION version: " << g_RELION_VERSION << " "

    #if defined(DEBUG) || defined(DEBUG_CUDA)
    << "(debug-build) "
    #endif

    << std::endl << "Precision: BASE="

    #ifdef RELION_SINGLE_PRECISION
    << "single"
    #else
    << "double"
    #endif

    #if defined(CUDA) || defined(ALTCPU)

        #ifdef CUDA
        << ", CUDA-ACC="
        #endif

        #ifdef ALTCPU
        << ", VECTOR-ACC="
        #endif

        #ifdef ACC_DOUBLE_PRECISION
        << "double "
        #else
        << "single "
        #endif

    #endif

    << std::endl << std::endl;

}

//@}
//@}
#endif
