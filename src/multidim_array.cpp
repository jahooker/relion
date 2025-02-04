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

#include "src/multidim_array.h"
#include <unordered_map>

// Show a complex array ---------------------------------------------------
std::ostream& operator << (std::ostream &ostrm, const MultidimArray<Complex> &v) {

    if (v.xdim == 0) {
        ostrm << "NULL MultidimArray\n";
    } else {
        ostrm << std::endl;
    }

    for (int l = 0; l < Nsize(v); l++) {
        if (Nsize(v) > 1) ostrm << "Image No. " << l << std::endl;
        for (int k = Zinit(v); k <= Zlast(v); k++) {
            if (Zsize(v) > 1) { ostrm << "Slice No. " << k << std::endl; }
            for (int j = Yinit(v); j <= Ylast(v); j++) {
                for (int i = Xinit(v); i <= Xlast(v); i++)
                    ostrm << "(" << v.elem(i, j, k).real << "," << v.elem(i, j, k).imag << ")" << ' ';
                ostrm << std::endl;
            }
        }
    }
    return ostrm;
}

template <typename T>
void threshold_abs_above(T *ptr, T a, T b) {
    if (abs(*ptr) > a) { *ptr = b * sgn(*ptr); }
}

template <typename T>
void threshold_abs_below(T *ptr, T a, T b) {
    if (abs(*ptr) < a) { *ptr = b * sgn(*ptr); }
}

template <typename T>
void threshold_above(T *ptr, T a, T b) {
    if (*ptr > a) { *ptr = b; }
}

template <typename T>
void threshold_below(T *ptr, T a, T b) {
    if (*ptr < a) { *ptr = b; }
}

template <typename T>
void threshold_range(T *ptr, T a, T b) {
    if (*ptr < a) { *ptr = a; } else
    if (*ptr > b) { *ptr = b; }
}

template <typename T, typename Allocator>
void MultidimArray<T, Allocator>::threshold(const std::string &type, T a, T b, MultidimArray<int> *mask) {

    static const std::unordered_map<std::string, void (*)(T *ptr, T a, T b)> s2f {
        {"abs_above", &threshold_abs_above},
        {"abs_below", &threshold_abs_below},
        {"above",     &threshold_above},
        {"below",     &threshold_below},
        {"range",     &threshold_range},
    };

    const auto it = s2f.find(type);
    if (it == s2f.end())
        REPORT_ERROR(static_cast<std::string>("Threshold: mode not supported (" + type + ")"));
    const auto f = it->second;

    int *maskptr = mask ? mask->begin() : nullptr;
    for (T *ptr = begin(); ptr != end(); ++ptr, ++maskptr) {
        if (!mask || *maskptr > 0) f(ptr, a, b);
    }
}

/** Array by array
 *
 * This function must take two vectors of the same size, and operate element
 * by element according to the operation required. This is the function
 * which really implements the operations. Simple calls to it perform much
 * faster than calls to the corresponding operators. Although it is supposed
 * to be a hidden function not useable by normal programmers.
 *
 */
template <typename T>
inline MultidimArray<T>& pointwise(
    MultidimArray<T> &lhs, const MultidimArray<T> &rhs, T (*operation)(T, T)
) {
    if (!lhs.sameShape(rhs)) {
        lhs.printShape();
        rhs.printShape();
        REPORT_ERROR((std::string) "Array_by_array: different shapes");
    }
    std::transform(
        lhs.begin(), lhs.begin() + lhs.xdim * lhs.ydim * lhs.zdim,  // Disregard slices
        rhs.begin(), lhs.begin(), operation
    );
    return lhs;
}

template <typename T, typename Allocator>
MultidimArray<T>& MultidimArray<T, Allocator>::operator += (const MultidimArray<T> &rhs) {
    return pointwise(*this, rhs, +[] (T x, T y) -> T { return x + y; });
}

template <typename T, typename Allocator>
MultidimArray<T>& MultidimArray<T, Allocator>::operator -= (const MultidimArray<T> &rhs) {
    return pointwise(*this, rhs, +[] (T x, T y) -> T { return x - y; });
}

template <typename T, typename Allocator>
MultidimArray<T>& MultidimArray<T, Allocator>::operator *= (const MultidimArray<T> &rhs) {
    return pointwise(*this, rhs, +[] (T x, T y) -> T { return x * y; });
}

template <typename T, typename Allocator>
MultidimArray<T>& MultidimArray<T, Allocator>::operator /= (const MultidimArray<T> &rhs) {
    return pointwise(*this, rhs, +[] (T x, T y) -> T { return x / y; });
}

#define INSTANTIATE_TEMPLATE(T) template class MultidimArray<T>;

INSTANTIATE_TEMPLATE(float)
INSTANTIATE_TEMPLATE(double)
INSTANTIATE_TEMPLATE(unsigned short)
INSTANTIATE_TEMPLATE(short)
INSTANTIATE_TEMPLATE(unsigned char)
INSTANTIATE_TEMPLATE(signed char)
INSTANTIATE_TEMPLATE(int)
INSTANTIATE_TEMPLATE(Complex)

#undef INSTANTIATE_TEMPLATE
