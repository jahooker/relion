/***************************************************************************
 *
 * Author: "Jasenko Zivanov"
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

#include "new_ft.h"

#include "src/macros.h"
#include "src/fftw.h"
#include "src/args.h"
#include <string.h>
#include <math.h>

pthread_mutex_t NewFFT::fftw_plan_mutex_new = PTHREAD_MUTEX_INITIALIZER;


void NewFFT::FourierTransform(
    MultidimArray<double> &src,
    MultidimArray<dComplex> &dest,
    const NewFFT::DoublePlan &plan,
    Normalization normalization
) {
    if (!plan.isCompatible(src)) {
        REPORT_ERROR("ERROR: plan incompatible with input array\n");
    }

    if (!plan.isCompatible(dest)) {
        if (plan.isReusable()) {
            dest.resizeNoCp(src.xdim / 2 + 1, src.ydim, src.zdim, src.ndim);
        } else {
            REPORT_ERROR("NewFFT::FourierTransform: plan incompatible with output array\n");
        }
    }

    _FourierTransform(src, dest, plan, normalization);
}

void NewFFT::inverseFourierTransform(
    MultidimArray<dComplex> &src,
    MultidimArray<double> &dest,
    const NewFFT::DoublePlan &plan,
    Normalization normalization,
    bool preserveInput
) {
    if (preserveInput && !plan.isReusable()) {
        REPORT_ERROR("NewFFT::inverseFourierTransform: preserveInput is only supported for reusable plans\n");
    }

    if (!plan.isCompatible(src)) {
        REPORT_ERROR("NewFFT::inverseFourierTransform: plan incompatible with input array\n");
    }

    if (!plan.isCompatible(dest)) {
        if (plan.isReusable()) {
            dest.resizeNoCp(2 * (src.xdim - 1), src.ydim, src.zdim, src.ndim);
        } else {
            REPORT_ERROR("NewFFT::inverseFourierTransform: plan incompatible with output array\n");
        }
    }

    MultidimArray<dComplex> src2;

    if (preserveInput) {
        src2 = src;
        _inverseFourierTransform(src2, dest, plan, normalization);
    } else {
        _inverseFourierTransform(src, dest, plan, normalization);
    }
}

void NewFFT::FourierTransform(
    MultidimArray<float> &src,
    MultidimArray<fComplex> &dest,
    const NewFFT::FloatPlan &plan,
    Normalization normalization
) {
    if (!plan.isCompatible(src)) {
        REPORT_ERROR("NewFFT::FourierTransform: plan incompatible with input array\n");
    }

    if (!plan.isCompatible(dest)) {
        if (plan.isReusable()) {
            dest.resizeNoCp(src.xdim / 2 + 1, src.ydim, src.zdim, src.ndim);
        } else {
            REPORT_ERROR("NewFFT::FourierTransform: plan incompatible with output array\n");
        }
    }

    _FourierTransform(src, dest, plan, normalization);
}

void NewFFT::inverseFourierTransform(
    MultidimArray<fComplex> &src,
    MultidimArray<float> &dest,
    const NewFFT::FloatPlan &plan,
    Normalization normalization,
    bool preserveInput
) {
    if (preserveInput && !plan.isReusable()) {
        REPORT_ERROR("NewFFT::inverseFourierTransform: preserveInput is only supported for reusable plans\n");
    }

    if (!plan.isCompatible(src)) {
        REPORT_ERROR("NewFFT::inverseFourierTransform: plan incompatible with input array\n");
    }

    if (!plan.isCompatible(dest)) {
        if (plan.isReusable()) {
            dest.resizeNoCp(2 * (src.xdim - 1), src.ydim, src.zdim, src.ndim);
        } else {
            REPORT_ERROR("NewFFT::inverseFourierTransform: plan incompatible with output array\n");
        }
    }

    MultidimArray<fComplex> src2;

    if (preserveInput) {
        src2 = src;
        _inverseFourierTransform(src2, dest, plan, normalization);
    } else {
        _inverseFourierTransform(src, dest, plan, normalization);
    }
}

void NewFFT::FourierTransform(
    MultidimArray<double> &src,
    MultidimArray<dComplex> &dest,
    Normalization normalization
) {
    if (!areSizesCompatible(src, dest)) {
        resizeComplexToMatch(src, dest);
    }

    DoublePlan p(src, dest);
    _FourierTransform(src, dest, p, normalization);
}

void NewFFT::inverseFourierTransform(
    MultidimArray<dComplex> &src,
    MultidimArray<double> &dest,
    Normalization normalization,
    bool preserveInput
) {
    if (!areSizesCompatible(dest, src)) {
        resizeRealToMatch(dest, src);
    }

    if (preserveInput) {
        MultidimArray<dComplex> src2 = src;
        DoublePlan p(dest, src2);
        _inverseFourierTransform(src2, dest, p, normalization);
    } else {
        DoublePlan p(dest, src);
        _inverseFourierTransform(src, dest, p, normalization);
    }
}

void NewFFT::FourierTransform(
    MultidimArray<float> &src,
    MultidimArray<fComplex> &dest,
    Normalization normalization
) {
    if (!areSizesCompatible(src, dest)) {
        resizeComplexToMatch(src, dest);
    }

    FloatPlan p(src, dest);
    _FourierTransform(src, dest, p, normalization);
}

void NewFFT::inverseFourierTransform(
    MultidimArray<fComplex> &src,
    MultidimArray<float> &dest,
    Normalization normalization,
    bool preserveInput
) {
    if (!areSizesCompatible(dest, src)) {
        resizeRealToMatch(dest, src);
    }

    if (preserveInput) {
        MultidimArray<fComplex> src2 = src;
        FloatPlan p(dest, src2);
        _inverseFourierTransform(src2, dest, p, normalization);
    } else {
        FloatPlan p(dest, src);
        _inverseFourierTransform(src, dest, p, normalization);
    }
}


void NewFFT::_FourierTransform(
    MultidimArray<double> &src,
    MultidimArray<dComplex> &dest,
    const NewFFT::DoublePlan &plan,
    Normalization normalization
) {
    fftw_execute_dft_r2c(plan.getForward(), src.data, (fftw_complex*) dest.data);

    if (normalization == FwdOnly) {
        const double scale = src.size();
        for (auto &x : dest) { x /= scale; }
    } else if (normalization == Both) {
        const double scale = sqrt(src.size());
        for (auto &x : dest) { x /= scale; }
    }
}

void NewFFT::_inverseFourierTransform(
    MultidimArray<dComplex> &src,
    MultidimArray<double> &dest,
    const NewFFT::DoublePlan &plan,
    Normalization normalization
) {
    fftw_complex *in = (fftw_complex*) src.data;

    fftw_execute_dft_c2r(plan.getBackward(), in, dest.data);

    if (normalization == Both) {
        const double scale = sqrt(dest.size());
        for (auto &x : dest) { x /= scale; }
    }
}

void NewFFT::_FourierTransform(
    MultidimArray<float> &src,
    MultidimArray<fComplex> &dest,
    const NewFFT::FloatPlan &plan,
    Normalization normalization
) {
    fftwf_execute_dft_r2c(plan.getForward(), src.data, (fftwf_complex*) dest.data);

    if (normalization == FwdOnly) {
        const float scale = src.size();
        for (auto &x : dest) { x /= scale; }
    } else if (normalization == Both) {
        const float scale = sqrt(src.size());
        for (auto &x : dest) { x /= scale; }
    }
}

void NewFFT::_inverseFourierTransform(
    MultidimArray<fComplex> &src,
    MultidimArray<float> &dest,
    const NewFFT::FloatPlan &plan,
    Normalization normalization
) {
    fftwf_complex *in = (fftwf_complex*) src.data;

    fftwf_execute_dft_c2r(plan.getBackward(), in, dest.data);

    if (normalization == Both) {
        const float scale = sqrt(dest.size());
        for (auto &x : dest) { x /= scale; }
    }
}


NewFFT::DoublePlan::DoublePlan(int w, int h, int d, unsigned int flags):
reusable(true),
w(w), h(h), d(d),
realPtr(0),
complexPtr(0)
{
    MultidimArray<double> realDummy(d, h, w);
    MultidimArray<dComplex> complexDummy(d, h, w / 2 + 1);

    std::vector<int> N(0);
    if (d > 1) N.push_back(d);
    if (h > 1) N.push_back(h);
               N.push_back(w);

    const int ndim = N.size();

    pthread_mutex_lock(&fftw_plan_mutex_new);

    fftw_plan planForward = fftw_plan_dft_r2c(
        ndim, &N[0],
        realDummy.data,
        (fftw_complex*) complexDummy.data,
        FFTW_UNALIGNED | flags
    );

    fftw_plan planBackward = fftw_plan_dft_c2r(
        ndim, &N[0],
        (fftw_complex*) complexDummy.data,
        realDummy.data,
        FFTW_UNALIGNED | flags
    );

    pthread_mutex_unlock(&fftw_plan_mutex_new);

    if (!planForward || !planBackward) {
        REPORT_ERROR("FFTW plans cannot be created");
    }

    plan = std::shared_ptr<Plan>(new Plan(planForward, planBackward));
}

NewFFT::DoublePlan::DoublePlan(
    MultidimArray<double> &real,
    MultidimArray<dComplex> &complex,
    unsigned int flags
):
reusable(flags & FFTW_UNALIGNED),
w(real.xdim), h(real.ydim), d(real.zdim),
realPtr(real.data),
complexPtr((double*)complex.data)
{
    std::vector<int> N(0);
    if (d > 1) N.push_back(d);
    if (h > 1) N.push_back(h);
               N.push_back(w);

    const int ndim = N.size();

    pthread_mutex_lock(&fftw_plan_mutex_new);

    fftw_plan planForward = fftw_plan_dft_r2c(
        ndim, &N[0],
        real.data,
        (fftw_complex*) complex.data,
        flags
    );

    fftw_plan planBackward = fftw_plan_dft_c2r(
        ndim, &N[0],
        (fftw_complex*) complex.data,
        real.data,
        flags
    );

    pthread_mutex_unlock(&fftw_plan_mutex_new);

    if (!planForward || !planBackward) {
        REPORT_ERROR("FFTW plans cannot be created");
    }

    plan = std::shared_ptr<Plan>(new Plan(planForward, planBackward));
}

NewFFT::FloatPlan::FloatPlan(int w, int h, int d, unsigned int flags):
reusable(true),
w(w), h(h), d(d),
realPtr(0),
complexPtr(0)
{
    MultidimArray<float> realDummy(d, h, w);
    MultidimArray<fComplex> complexDummy(d, h, w / 2 + 1);

    std::vector<int> N(0);
    if (d > 1) N.push_back(d);
    if (h > 1) N.push_back(h);
               N.push_back(w);

    const int ndim = N.size();

    pthread_mutex_lock(&fftw_plan_mutex_new);

    fftwf_plan planForward = fftwf_plan_dft_r2c(
        ndim, &N[0],
        realDummy.data,
        (fftwf_complex*) complexDummy.data,
        FFTW_UNALIGNED | flags
    );

    fftwf_plan planBackward = fftwf_plan_dft_c2r(
        ndim, &N[0],
        (fftwf_complex*) complexDummy.data,
        realDummy.data,
        FFTW_UNALIGNED | flags
    );

    pthread_mutex_unlock(&fftw_plan_mutex_new);

    if (!planForward || !planBackward) {
        REPORT_ERROR("FFTW plans cannot be created");
    }

    plan = std::shared_ptr<Plan>(new Plan(planForward, planBackward));
}

NewFFT::FloatPlan::FloatPlan(
    MultidimArray<float> &real,
    MultidimArray<fComplex> &complex,
    unsigned int flags
):
reusable(flags & FFTW_UNALIGNED),
w(real.xdim), h(real.ydim), d(real.zdim),
realPtr(real.data),
complexPtr((float*) complex.data)
{
    std::vector<int> N(0);
    if (d > 1) N.push_back(d);
    if (h > 1) N.push_back(h);
               N.push_back(w);

    const int ndim = N.size();

    pthread_mutex_lock(&fftw_plan_mutex_new);

    fftwf_plan planForward = fftwf_plan_dft_r2c(
        ndim, &N[0],
        real.data,
        (fftwf_complex*) complex.data,
        flags
    );

    fftwf_plan planBackward = fftwf_plan_dft_c2r(
        ndim, &N[0],
        (fftwf_complex*) complex.data,
        real.data,
        flags
    );

    pthread_mutex_unlock(&fftw_plan_mutex_new);

    if (!planForward || !planBackward) {
        REPORT_ERROR("FFTW plans cannot be created");
    }

    plan = std::shared_ptr<Plan>(new Plan(planForward, planBackward));
}
