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

#include "motion_refinement.h"

#include <src/jaz/img_proc/filter_helper.h>
#include <src/jaz/img_proc/image_op.h>
#include <src/jaz/image_log.h>
#include <src/jaz/interpolation.h>
#include <src/jaz/optimization/nelder_mead.h>
#include <src/jaz/Fourier_helper.h>
#include <src/jaz/optimization/gradient_descent.h>
#include <omp.h>

using namespace gravis;

Image<RFLOAT> MotionRefinement::recompose(
    const std::vector<Image<RFLOAT>> &obs, const std::vector<double> &pos
) {
    const int w = obs[0].data.xdim;
    const int h = obs[0].data.ydim;
    const int ic = obs.size();

    Image<Complex> outC = Image<Complex>::zeros(w / 2 + 1, h);

    FourierTransformer ft;

    for (int i = 0; i < ic; i++) {
        Image<RFLOAT> obs2 = obs[i];
        Image<Complex> imgC;
        imgC() = ft.FourierTransform(obs2());

        if (pos[2 * i] != 0.0 || pos[2 * i + 1] != 0.0) {
            shiftImageInFourierTransform(imgC(), imgC.data.ydim, -pos[2 * i], -pos[2 * i + 1]);
        }

        ImageOp::linearCombination(imgC, outC, 1.0, 1.0 / (double) ic, outC);
    }

    Image<RFLOAT> out(w, h);
    out() = ft.inverseFourierTransform(outC());
    return out;
}

Image<RFLOAT> MotionRefinement::recompose(
    const std::vector<Image<Complex>> &obs, const std::vector<double> &pos
) {
    const int w = 2 * obs[0].data.xdim - 1;
    const int h = obs[0].data.ydim;
    const int ic = obs.size();

    Image<Complex> outC = Image<Complex>::zeros(obs[0].data.xdim, obs[0].data.ydim);

    FourierTransformer ft;
    Image<Complex> imgC;

    for (int i = 0; i < ic; i++) {
        imgC = obs[i];

        if (pos[2 * i] != 0.0 || pos[2 * i + 1] != 0.0) {
            shiftImageInFourierTransform(
                imgC(), imgC.data.ydim, -pos[2 * i], -pos[2 * i + 1]
            );
        }

        ImageOp::linearCombination(imgC, outC, 1.0, 1.0 / (double) ic, outC);
    }


    Image<RFLOAT> out(w, h);
    out() = ft.inverseFourierTransform(outC());
    return out;
}

Image<RFLOAT> MotionRefinement::averageStack(
    const std::vector<Image<RFLOAT>> &obs
) {
    Image<RFLOAT> out = Image<RFLOAT>::zeros(obs[0].data.xdim, obs[0].data.ydim);

    const int ic = obs.size();

    for (int i = 0; i < ic; i++) {
        ImageOp::linearCombination(out, obs[i], 1.0, 1.0 / (double) ic, out);
    }

    return out;
}

Image<RFLOAT> MotionRefinement::averageStack(
    const std::vector<Image<Complex>> &obs
) {
    Image<Complex> outC = Image<Complex>::zeros(obs[0].data.xdim, obs[0].data.ydim);

    const int ic = obs.size();

    for (int i = 0; i < ic; i++) {
        ImageOp::linearCombination(outC, obs[i], 1.0, 1.0 / (double) ic, outC);
    }

    Image<RFLOAT> outR(2 * obs[0].data.xdim - 1, obs[0].data.ydim);
    FourierTransformer ft;
    outR() = ft.inverseFourierTransform(outC());
    return outR;
}

std::vector<std::vector<Image<RFLOAT>>> MotionRefinement::movieCC(
    Projector& projector0,
    Projector& projector1,
    const ObservationModel &obsModel,
    MetaDataTable &viewParams,
    const std::vector<std::vector<Image<Complex>>> &movie,
    const std::vector<double> &sigma2,
    const std::vector<Image<RFLOAT>> &damageWeights,
    std::vector<ParFourierTransformer>& fts, int threads
) {
    const int pc = movie.size();
    const int fc = movie[0].size();

    const int s = movie[0][0]().ydim;
    const int sh = s / 2 + 1;

    std::vector<std::vector<Image<RFLOAT>>> out(pc);

    std::vector<Image<Complex>> ccsFs(threads);
    std::vector<Image<RFLOAT>> ccsRs(threads);

    for (int t = 0; t < threads; t++) {
        ccsFs[t] = Image<Complex>(s, sh);
        ccsFs[t].data.xinit = 0;
        ccsFs[t].data.yinit = 0;

        ccsRs[t] = Image<RFLOAT>(s, s);
        ccsRs[t].data.xinit = 0;
        ccsRs[t].data.yinit = 0;
    }

    Image<Complex> pred;

    for (int p = 0; p < pc; p++) {
        out[p] = std::vector<Image<RFLOAT>>(fc);

        for (int f = 0; f < fc; f++) {
            out[p][f] = Image<RFLOAT>(s,s);
        }

        int randSubset = viewParams.getValue(EMDL::PARTICLE_RANDOM_SUBSET, p) - 1;

        pred = obsModel.predictObservation(
            randSubset == 0 ? projector0 : projector1, viewParams, p, true, true
        );

        noiseNormalize(pred, sigma2, pred);

        #pragma omp parallel for num_threads(threads)
        for (int f = 0; f < fc; f++) {
            int t = omp_get_thread_num();

            for (int y = 0; y < s; y++)
            for (int x = 0; x < sh; x++) {
                ccsFs[t](y, x) = movie[p][f](y, x) * damageWeights[f](y, x) * pred(y, x).conj();
            }

            ccsRs[t]() = fts[t].inverseFourierTransform(ccsFs[t]());

            for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                out[p][f](y, x) = s * s * ccsRs[t](y, x);
            }
        }
    }

    return out;
}

std::vector<d2Vector> MotionRefinement::getGlobalTrack(
    const std::vector<std::vector<Image<RFLOAT>>>& movieCC
) {
    const int pc = movieCC.size();
    const int fc = movieCC[0].size();

    const int s = movieCC[0][0]().xdim;
    const int sh = s / 2 + 1;

    std::vector<d2Vector> out(fc);
    const double eps = 1e-30;

    std::vector<Image<RFLOAT>> e_sum(fc);

    for (int f = 0; f < fc; f++) {
        e_sum[f] = Image<RFLOAT>::zeros(s, s);

        for (int p = 0; p < pc; p++) {
            for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                e_sum[f](y, x) += movieCC[p][f](y, x);
            }
        }

        d2Vector pos = Interpolation::quadraticMaxWrapXY(e_sum[f], eps);

        if (pos.x >= sh) pos.x -= s;
        if (pos.y >= sh) pos.y -= s;

        out[f] = pos;
    }

    return out;
}

std::vector<Image<RFLOAT>> MotionRefinement::addCCs(
    const std::vector<std::vector<Image<RFLOAT>>> &movieCC
) {
    const int pc = movieCC.size();
    const int fc = movieCC[0].size();

    const int s = movieCC[0][0]().xdim;

    std::vector<Image<RFLOAT>> e_sum(fc);

    for (int f = 0; f < fc; f++) {
        e_sum[f] = Image<RFLOAT>::zeros(s, s);

        for (int p = 0; p < pc; p++) {
            for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                e_sum[f](y,x) += movieCC[p][f](y,x);
            }
        }
    }

    return e_sum;
}

std::vector<d2Vector> MotionRefinement::getGlobalTrack(
    const std::vector<Image<RFLOAT>> &movieCcSum
) {
    const int fc = movieCcSum.size();

    const int s = movieCcSum[0]().xdim;
    const int sh = s / 2 + 1;

    std::vector<d2Vector> out(fc);
    const double eps = 1e-30;

    for (int f = 0; f < fc; f++) {
        d2Vector pos = Interpolation::quadraticMaxWrapXY(movieCcSum[f], eps);

        if (pos.x >= sh) pos.x -= s;
        if (pos.y >= sh) pos.y -= s;

        out[f] = pos;
    }

    return out;
}

std::vector<d2Vector> MotionRefinement::getGlobalOffsets(
    const std::vector<std::vector<Image<RFLOAT>>>& movieCC,
    const std::vector<d2Vector>& globTrack, double sigma, int threads
) {
    const int pc = movieCC.size();
    const int fc = movieCC[0].size();
    const int s = movieCC[0][0]().xdim;
    const int sh = s / 2 + 1;
    const double eps = 1e-30;

    std::vector<d2Vector> out(pc);
    Image<RFLOAT> weight(s, s);

    for (int y = 0; y < s; y++)
    for (int x = 0; x < s; x++) {
        double xx = x >= sh ? x - s : x;
        double yy = y >= sh ? y - s : y;

        weight(y, x) = exp(-0.5 * (xx * xx + yy * yy) / (sigma * sigma));
    }

    #pragma omp parallel for num_threads(threads)
    for (int p = 0; p < pc; p++) {
        Image<RFLOAT> pSum = Image<RFLOAT>::zeros(s, s);

        for (int f = 0; f < fc; f++) {
            const d2Vector g = globTrack[f];

            for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                pSum(y, x) += Interpolation::cubicXY(movieCC[p][f], x + g.x, y + g.y, 0, 0, true);
            }
        }

        for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++) {
            pSum(y, x) *= weight(y, x);
        }

        d2Vector out_p = Interpolation::quadraticMaxWrapXY(pSum, eps);
        if (out_p.x >= sh) { out_p.x -= s; }
        if (out_p.y >= sh) { out_p.y -= s; }

        #pragma omp_atomic
        out[p] = out_p;
    }

    return out;
}

Image<float> MotionRefinement::crossCorrelation2D(
    const Image<Complex> &obs, const Image<Complex> &predConj,
    const Image<RFLOAT> &wgh, const std::vector<double>& sigma2
) {
    int wf = obs().xdim;
    int w = 2 * wf - 1;
    int h = obs().ydim;

    Image<Complex> prod, prod2;

    ImageOp::multiply(obs, predConj, prod);
    ImageOp::multiply(wgh, prod, prod2);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < wf; x++) {
        if (x == 0 && y == 0) continue;

        const double yy = y < wf ? y : y - h;
        const double xx = x;

        const int r = (int) sqrt(xx * xx + yy * yy);

        if (r >= wf) {
            direct::elem(prod2.data, x, y) = 0.0;
        } else {
            direct::elem(prod2.data, x, y) /= sigma2[r];
        }
    }

    Image<RFLOAT> corr(w, h);
    FourierTransformer ft;
    corr() = ft.inverseFourierTransform(prod2());

    Image<float> out(w,h);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        direct::elem(out.data, x, y) = (float) direct::elem(corr.data, (x + w / 2 - 1) % w, (y + h / 2 - 1) % h);
    }

    return out;
}

Image<float> MotionRefinement::crossCorrelation2D(
    const Image<Complex> &obs, const Image<Complex> &predConj,
    const std::vector<double>& sigma2, bool probability, bool normalize
) {
    int wf = obs().xdim;
    int w = 2 * wf - 1;
    int h = obs().ydim;

    /*{
        Image<Complex> obsM(wf,h), predM(wf,h);

        for (int y = 0; y < h; y++)
        for (int x = 0; x < wf; x++)
        {
            if (x == 0 && y == 0)
            {
                direct::elem(obs.data, x, y) = 0.0;
                direct::elem(predConj.data, x, y) = 0.0;
                continue;
            }

            const double yy = y < wf? y : y - h;
            const double xx = x;

            const int r = (int) sqrt(xx*xx + yy*yy);

            if (r >= wf)
            {
                direct::elem(obs.data, x, y) = 0.0;
                direct::elem(predConj.data, x, y) = 0.0;
            }
            else
            {
                direct::elem(obs.data, x, y) /= sqrt(0.25*PI*w*h*sigma2[r]);
                direct::elem(predConj.data, x, y) /= sqrt(0.25*PI*w*h*sigma2[r]);
            }
        }

        for (int y = 0; y < h; y++)
        for (int x = 0; x < wf; x++)
        {
            direct::elem(obsM.data, x, y) = direct::elem(obs.data, x, y);
            direct::elem(predM.data, x, y) = direct::elem(predConj.data, x, y).conj();
        }

        Image<RFLOAT> obsR(w,h), predR(w,h);

        FourierTransformer ft;
        obsR()  = ft.inverseFourierTransform(obsM());
        predR() = ft.inverseFourierTransform(predM());

        double muObs = 0.0;
        double muPred = 0.0;
        double varObs = 0.0;
        double varPred = 0.0;

        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            muObs += direct::elem(obsR.data, x, y);
            muPred += direct::elem(predR.data, x, y);
            varObs += direct::elem(obsR.data, x, y) * direct::elem(obsR.data, x, y);
            varPred += direct::elem(predR.data, x, y) * direct::elem(predR.data, x, y);
        }

        muObs /= w*h;
        muPred /= w*h;
        varObs /= w*h;
        varPred /= w*h;

        std::cout << "mu: " << muObs << ", " << muPred << "\n";
        std::cout << "s2: " << varObs << ", " << varPred << "\n";

        VtkHelper::writeVTK(obsR, "corrDebug/obsR.vtk");
        VtkHelper::writeVTK(predR, "corrDebug/predR.vtk");

        Image<Complex> corrF(wf,h);

        for (int y = 0; y < h; y++)
        for (int x = 0; x < wf; x++)
        {
            direct::elem(corrF.data, x, y) = direct::elem(obsM.data, x, y)
                    * direct::elem(predM.data, x, y).conj();
        }

        Image<RFLOAT> corrR(w,h);
        corrR() = ft.inverseFourierTransform(corrF());

        VtkHelper::writeVTK(corrR, "corrDebug/corrR_FS.vtk");

        Image<double> corrR2(w,h);

        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            double cc = 0;

            for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
            {
                double vo = direct::elem(obsR.data, xx, yy);
                double vp = direct::elem(predR.data, (xx+x)%w, (yy+y)%h);

                cc += vo * vp;
            }

            direct::elem(corrR2.data, x, y) = cc;
        }

        VtkHelper::writeVTK(corrR2, "corrDebug/corrR_RS.vtk");

        std::exit(0);
    }*/

    Image<Complex> prod;

    ImageOp::multiply(obs, predConj, prod);

    const double area = 0.25 * PI * w * h;

    if (probability) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < wf; x++) {
            if (x == 0 && y == 0) continue;

            const double yy = y < wf ? y : y - h;
            const double xx = x;

            const int r = (int) sqrt(xx * xx + yy * yy);

            if (r >= wf) {
                direct::elem(prod.data, x, y) = 0.0;
            } else {
                direct::elem(prod.data, x, y) /= sigma2[r] * area;
            }
        }
    }


    direct::elem(prod.data, 0, 0) = 0.0;

    Image<RFLOAT> corr(w, h);
    corr() = FourierTransformer{}.inverseFourierTransform(prod());

    Image<float> out(w,h);

    if (probability) {
        if (normalize) {
            double sum = 0.0;

            for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                sum += exp(w * h * direct::elem(corr.data, (x + w / 2) % w), (y + h / 2) % h);
            }

            for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                direct::elem(out.data, x, y) = exp(w * h * direct::elem(corr.data, (x + w / 2) % w, (y + h / 2) % h)) / sum;
            }
        } else {
            for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                direct::elem(out.data, x, y) = (float) exp(w * h * direct::elem(corr.data, (x + w / 2) % w, (y + h / 2) % h));
            }
        }
    } else {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            direct::elem(out.data, x, y) = (float) (w * h * direct::elem(corr.data, (x + w / 2) % w), (y + h / 2) % h);
        }
    }

    return out;
}

void MotionRefinement::noiseNormalize(
    const Image<Complex> &img, const std::vector<double> &sigma2, Image<Complex>& dest
) {
    int wf = img().xdim;
    int w = 2 * wf - 1;
    int h = img().ydim;

    const double area = 0.25 * PI * w * h;

    if (dest.data.xdim != img.data.xdim || dest.data.ydim != img.data.ydim) {
        dest.data.reshape(img.data);
    }

    dest.data.xinit = 0;
    dest.data.yinit = 0;

    for (int y = 0; y < h; y++)
    for (int x = 0; x < wf; x++) {
        if (x == 0 && y == 0) {
            dest(y, x) = Complex(0.0);
            continue;
        }

        const double yy = y < wf ? y : y - h;
        const double xx = x;

        const int r = (int) sqrt(xx * xx + yy * yy);

        dest(y, x) = r >= wf ? Complex(0.0) : direct::elem(img.data, x, y) / sqrt(sigma2[r] * area);
    }
}

std::vector<std::vector<d2Vector>> MotionRefinement::readTrack(
    std::string fn, int pc, int fc
) {
    std::vector<std::vector<d2Vector>> shift(pc);

    std::ifstream trackIn(fn);

    for (int p = 0; p < pc; p++) {
        if (!trackIn.good()) {
            std::cerr << "MotionRefinement::readTrack: error reading tracks in " << fn << "\n";
            REPORT_ERROR("MotionRefinement::readTrack: error reading tracks in " + fn + "\n");
        }

        shift[p] = std::vector<d2Vector>(fc);

        char dummy[4069];
        trackIn.getline(dummy, 4069);

        for (int f = 0; f < fc; f++) {
            char dummy[4069];
            trackIn.getline(dummy, 4069);

            std::istringstream sts(dummy);

            sts >> shift[p][f].x;
            sts >> shift[p][f].y;
        }

        trackIn.getline(dummy, 4069);
    }

    return shift;
}

void MotionRefinement::writeTracks(
    const std::vector<std::vector<d2Vector>>& tracks, std::string fn
) {
    const int pc = tracks.size();
    const int fc = tracks[0].size();

    std::string path = fn.substr(0, fn.find_last_of('/'));
    mktree(path);

    std::ofstream ofs(fn);
    MetaDataTable mdt;

    mdt.name = "general";
    mdt.isList = true;
    mdt.setValue(EMDL::PARTICLE_NUMBER, pc, mdt.addObject());

    mdt.write(ofs);
    mdt.clear();

    for (int p = 0; p < pc; p++) {
        mdt.name = std::to_string(p);

        for (int f = 0; f < fc; f++) {
            mdt.addObject();
            mdt.setValue(EMDL::ORIENT_ORIGIN_X, tracks[p][f].x, f);
            mdt.setValue(EMDL::ORIENT_ORIGIN_Y, tracks[p][f].y, f);
        }

        mdt.write(ofs);
        mdt.clear();
    }
}

std::vector<std::vector<d2Vector>> MotionRefinement::readTracks(std::string fn) {

    std::ifstream ifs(fn);
    if (ifs.fail()) {
        REPORT_ERROR("MotionRefinement::readTracks: unable to read " + fn + ".");
    }

    MetaDataTable mdt;
    mdt.readStar(ifs, "general");

    int pc;
    try {
        pc = mdt.getValue(EMDL::PARTICLE_NUMBER);
    } catch (const char* errmsg) {
        REPORT_ERROR("MotionRefinement::readTracks: missing particle number in " + fn + ".");
    }

    std::vector<std::vector<d2Vector>> out(pc);
    int fc = 0, lastFc = 0;

    for (int p = 0; p < pc; p++) {
        mdt.readStar(ifs, std::to_string(p));

        fc = mdt.size();

        if (p > 0 && fc != lastFc) {
            REPORT_ERROR("MotionRefinement::readTracks: broken file: "+fn+".");
        }

        lastFc = fc;

        out[p] = std::vector<d2Vector>(fc);
        for (int f = 0; f < fc; f++) {
            out[p][f].x = mdt.getValue(EMDL::ORIENT_ORIGIN_X, f);
            out[p][f].y = mdt.getValue(EMDL::ORIENT_ORIGIN_Y, f);
        }
    }

    return out;
}

d3Vector MotionRefinement::measureValueScaleReal(
    const Image<Complex>& data,
    const Image<Complex>& ref
) {
    int wf = data().xdim;
    int w = 2 * wf - 1;
    int h = data().ydim;

    Image<Complex> dataC = data, refC = ref;
    direct::elem(dataC.data, 0, 0) = Complex(0.0);
    direct::elem(refC .data, 0, 0) = Complex(0.0);

    Image<RFLOAT> dataR(w, h), refR(w, h);

    FourierTransformer ft;
    dataR() = ft.inverseFourierTransform(dataC());
    refR () = ft.inverseFourierTransform(refC ());

    double num = 0.0;
    double denom = 0.0;

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        RFLOAT d = direct::elem(dataR.data, x, y);
        RFLOAT r = direct::elem(refR.data, x, y);

        num   += d * r;
        denom += r * r;
    }

    /*{
        VtkHelper::writeVTK(dataR, "debug/dataR.vtk");
        VtkHelper::writeVTK(refR, "debug/refR.vtk");

        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            direct::elem(refR.data, x, y) *= (denom/num);
        }

        VtkHelper::writeVTK(refR, "debug/refR2.vtk");

        std::exit(0);
    }*/

    return d3Vector(num / denom, num, denom);
}

d3Vector MotionRefinement::measureValueScale(
    const Image<Complex>& data, const Image<Complex>& ref
) {
    int w = data().xdim;
    int h = data().ydim;

    double num = 0.0;
    double denom = 0.0;

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        double d = direct::elem(data.data, x, y).abs();
        double r = direct::elem(ref .data, x, y).abs();

        num   += d * r;
        denom += d * d;
    }

    return d3Vector(num / denom, num, denom);
}

void MotionRefinement::testCC(
    const Image<Complex> &obs, const Image<Complex> &predConj, 
    const std::vector<double> &sigma2
) {
    int wf = obs().xdim;
    int w = 2 * wf - 1;
    int h = obs().ydim;

    Image<Complex> obsW(wf, h), predW(wf, h);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < wf; x++) {
        const double yy = y < wf ? y : y - h;
        const double xx = x;

        const int r = (int) sqrt(xx * xx + yy * yy);

        if (r == 0 || r >= wf) {
            direct::elem(obsW .data, x, y) = 0.0;
            direct::elem(predW.data, x, y) = 0.0;
        } else {
            direct::elem(obsW .data, x, y) = direct::elem(obs     .data, x, y)        / sqrt(sigma2[r]);
            direct::elem(predW.data, x, y) = direct::elem(predConj.data, x, y).conj() / sqrt(sigma2[r]);
        }
    }

    std::vector<double> sig2new(wf, 0.0), wgh(wf, 0.0);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < wf; x++) {
        const Complex z = direct::elem(obsW.data, x, y);

        const double yy = y < w ? y : y - h;
        const double xx = x;

        const int r = (int) sqrt(xx * xx + yy * yy);

        if (r >= wf) continue;

        sig2new[r] += z.norm();
        wgh[r] += 1.0;
    }

    for (int x = 0; x < wf; x++) {
        if (wgh[x] > 0.0) {
            sig2new[x] /= wgh[x];
        }
    }

    std::ofstream ofs("spec_new.dat");

    for (int x = 0; x < wf; x++) {
        ofs << x << " " << sig2new[x] << "\n";
    }

    FourierTransformer ft;

    Image<RFLOAT> obsWR(w, h), predWR(w, h);
    obsWR()  = ft.inverseFourierTransform(obsW());
    predWR() = ft.inverseFourierTransform(predW());

    ImageLog::write(obsWR, "debug/obsWR");
    ImageLog::write(predWR, "debug/predWR");

    double var = 0.0;

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        double v = direct::elem(obsWR.data, x, y);
        var += v * v;
    }

    var /= w * h;

    std::cout << "var real: " << var << " = " << PI * w * h / 4.0 << "?\n";

    Image<RFLOAT> corrR = Image<RFLOAT>::zeros(w, h);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        double cc = 0.0;

        for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            RFLOAT v0 = direct::elem(predWR.data, xx, yy);
            RFLOAT v1 = direct::elem(obsWR.data, (xx + x) % w, (yy + y) % h);

            cc += v0 * v1;
        }

        direct::elem(corrR.data, x, y) = cc;
    }

    ImageLog::write(corrR, "debug/Wcc_rs");

    Image<RFLOAT> corr(w, h);

    Image<Complex> prod;

    ImageOp::multiply(obs, predConj, prod);

    for (int y = 0; y < h; y++)
    for (int x = 0; x < wf; x++) {
        if (x == 0 && y == 0) continue;

        const double yy = y < wf ? y : y - h;
        const double xx = x;

        const int r = (int) sqrt(xx * xx + yy * yy);

        if (r >= wf) {
            direct::elem(prod.data, x, y) = 0.0;
        } else {
            direct::elem(prod.data, x, y) /= sigma2[r];
        }
    }

    corr() = ft.inverseFourierTransform(prod());

    for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
        direct::elem(corr.data, x, y) *= w * h;
    }

    ImageLog::write(corr, "debug/Wcc_fs");
}

Image<RFLOAT> MotionRefinement::zeroPad(
    const Image<RFLOAT>& img, RFLOAT ratio, RFLOAT taper
) {
    const long w = img.data.xdim;
    const long h = img.data.ydim;

    const long ww = (long) (ratio * w);
    const long hh = (long) (ratio * h);

    const long tx = (long) (taper * (RFLOAT)w);
    const long ty = (long) (taper * (RFLOAT)h);

    Image<RFLOAT> out = Image<RFLOAT>::zeros(ww, hh);

    const long x0 = (ww - w) / 2;
    const long y0 = (hh - h) / 2;

    RFLOAT avg = 0.0;

    for (long y = 0; y < h; y++)
    for (long x = 0; x < w; x++) {
        avg += direct::elem(img.data, x, y);
    }

    avg /= (w * h);

    for (long y = 0; y < h; y++)
    for (long x = 0; x < w; x++) {
        RFLOAT tw = 1.0;

        if (x < tx || x >= w-tx || y < ty || y >= h-ty) {
            RFLOAT ex0 = x < tx ?      (x + 1) / (RFLOAT) (tx + 1) : 1.0;
            RFLOAT ex1 = x >= w - tx ? (w - x) / (RFLOAT) (tx + 1) : 1.0;
            RFLOAT ey0 = y < ty ?      (y + 1) / (RFLOAT) (ty + 1) : 1.0;
            RFLOAT ey1 = y >= h - ty ? (h - y) / (RFLOAT) (ty + 1) : 1.0;

            ex0 = (1.0 - cos(PI * ex0)) / 2.0;
            ex1 = (1.0 - cos(PI * ex1)) / 2.0;
            ey0 = (1.0 - cos(PI * ey0)) / 2.0;
            ey1 = (1.0 - cos(PI * ey1)) / 2.0;

            tw = ex0 * ex1 * ey0 * ey1;
        }

        direct::elem(out.data, x + x0, y + y0) += tw * (direct::elem(img.data, x, y) - avg);
    }

    return out;
}

std::vector<Image<float>> MotionRefinement::collectiveMotion(
    const std::vector<std::vector<Image<float>>>& correlation
) {
    const int pc = correlation.size();

    if (pc == 0) return std::vector<Image<float>>(0);

    const int fc = correlation[0].size();

    std::vector<Image<float>> corrSum(fc);

    const int w = correlation[0][0].data.xdim;
    const int h = correlation[0][0].data.ydim;

    for (int f = 0; f < fc; f++) {
        corrSum[f] = Image<float>::zeros(w, h);
    }

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            ImageOp::linearCombination(corrSum[f], correlation[p][f], 1.0, 1.0, corrSum[f]);
        }
    }

    return corrSum;
}

std::vector<std::vector<Image<float>>> MotionRefinement::blockMotion(
    const std::vector<std::vector<Image<float>>>& correlation,
    std::vector<d2Vector> positions, int parts, int micrographWidth,
    std::vector<int> &numbers
) {
    const int pc = correlation.size();

    if (pc == 0) return std::vector<std::vector<Image<float>>>(0);

    const int fc = correlation[0].size();

    const int w = correlation[0][0].data.xdim;
    const int h = correlation[0][0].data.ydim;
    const int qc = parts * parts;

    std::vector<std::vector<Image<float>>> corrSum(qc);

    for (int q = 0; q < qc; q++) {
        corrSum[q] = std::vector<Image<float>>(fc);

        for (int f = 0; f < fc; f++) {
            corrSum[q][f] = Image<float>::zeros(w, h);
        }
    }

    for (int p = 0; p < pc; p++) {
        int qx = (int)(parts * positions[p].x / micrographWidth);
        int qy = (int)(parts * positions[p].y / micrographWidth);

        if (qx > parts || qy > parts) continue;

        int q = qy * parts + qx;

        numbers[q]++;

        for (int f = 0; f < fc; f++) {
            ImageOp::linearCombination(corrSum[q][f], correlation[p][f], 1.0, 1.0, corrSum[q][f]);
        }
    }

    return corrSum;
}

std::vector<d2Vector> MotionRefinement::findMaxima(
    std::vector<Image<float>> & corrSum
) {
    const int fc = corrSum.size();

    const int w = corrSum[0].data.xdim;
    const int h = corrSum[0].data.ydim;
    const double cx = w / 2;
    const double cy = h / 2;

    std::vector<gravis::d2Vector> out(fc);

    for (int f = 0; f < fc; f++) {
        d2Vector m = Interpolation::quadraticMaxXY(corrSum[f]);
        out[f] = d2Vector(m.x - cx, m.y - cy);
    }

    return out;
}


std::vector<std::vector<gravis::d2Vector>> MotionRefinement::computeInitialPositions(
    const std::vector<std::vector<Image<float>>> &correlation
) {
    const int pc = correlation.size();

    if (pc == 0) return std::vector<std::vector<gravis::d2Vector>>(0);

    std::vector<Image<float>> corrSum = collectiveMotion(correlation);
    std::vector<gravis::d2Vector> maxima = findMaxima(corrSum);

    std::vector<std::vector<gravis::d2Vector>> out(pc, maxima);

    return out;
}

std::vector<std::vector<gravis::d2Vector>> MotionRefinement::optimize(
    const std::vector<std::vector<Image<float>>>& correlation,
    const std::vector<gravis::d2Vector>& positions,
    const std::vector<std::vector<gravis::d2Vector>>& initial,
    double lambda, double mu, double sigma
) {
    const int pc = correlation.size();

    if (pc == 0) return std::vector<std::vector<gravis::d2Vector>>(0);

    const int fc = correlation[0].size();

    std::vector<std::vector<RFLOAT>> distWeights(pc);

    const double s2 = sigma * sigma;

    for (int p = 0; p < pc; p++) {
        distWeights[p] = std::vector<RFLOAT>(pc);

        for (int q = 0; q < pc; q++) {
            const double d2 = (positions[p] - positions[q]).norm2();
            distWeights[p][q] = exp(-d2 / s2);
        }
    }

    MotionFit mf(correlation, distWeights, lambda, mu);

    std::vector<double> x0 = pack(initial);

    std::cout << "initial f      = " << mf.f(x0, 0) << "\n";
    std::cout << "initial f_data = " << mf.f_data(x0) << "\n";

    /*NelderMead nm;

    std::vector<double> final = nm.optimize(
                x0, mf, 1.0, 1e-5, 10000,
                1.0, 2.0, 0.5, 0.5, true);*/

    std::vector<double> final = GradientDescent::optimize(
        x0, mf, 1.0, 1e-20, 100000, 0.0, 0.0, true
    );

    return unpack(final, pc, fc);
}

std::vector<std::vector<Image<RFLOAT>> > MotionRefinement::visualize(
    const std::vector<std::vector<gravis::d2Vector>>& positions, 
    int pc, int fc, int w, int h
) {
    std::vector<std::vector<Image<RFLOAT>>> out(pc);

    for (int p = 0; p < pc; p++) {
        out[p].resize(fc);

        for (int f = 0; f < fc; f++) {
            out[p][f] = Image<RFLOAT>::zeros(w, h);

            gravis::d2Vector pos(positions[p][f].x + w / 2, positions[p][f].y + h / 2);

            int xi = (int) pos.x;
            int yi = (int) pos.y;
            double xf = pos.x - xi;
            double yf = pos.y - yi;

            if (xi >= 0 && xi < w && yi >= 0 && yi < h) {
                direct::elem(out[p][f].data, xi, yi) = (1.0 - xf) * (1.0 - yf);
            }
            if (xi + 1 >= 0 && xi + 1 < w && yi >= 0 && yi < h) {
                direct::elem(out[p][f].data, xi + 1, yi) = xf * (1.0 - yf);
            }
            if (xi >= 0 && xi < w && yi + 1 >= 0 && yi + 1 < h) {
                direct::elem(out[p][f].data, xi, yi + 1) = (1.0 - xf) * yf;
            }
            if (xi + 1 >= 0 && xi + 1 < w && yi + 1 >= 0 && yi + 1 < h) {
                direct::elem(out[p][f].data, xi + 1, yi + 1) = xf * yf;
            }
        }
    }

    return out;
}

std::vector<Image<RFLOAT>> MotionRefinement::collapsePaths(
    const std::vector<std::vector<Image<RFLOAT>>>& paths
) {
    const int pc = paths.size();

    if (pc == 0) return std::vector<Image<RFLOAT>>(0);

    const int fc = paths[0].size();

    std::vector<Image<RFLOAT>> pathSum(pc);

    const int w = paths[0][0].data.xdim;
    const int h = paths[0][0].data.ydim;

    for (int p = 0; p < pc; p++) {
        pathSum[p] = Image<RFLOAT>::zeros(w, h);
    }

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            ImageOp::linearCombination(pathSum[p], paths[p][f], 1.0, 1.0, pathSum[p]);
        }
    }

    return pathSum;
}

std::vector<std::vector<gravis::d2Vector>> MotionRefinement::unpack(
    const std::vector<double> &pos, int pc, int fc
) {
    std::vector<std::vector<gravis::d2Vector>> out(pc);

    for (int p = 0; p < pc; p++) {
        out[p].resize(fc);

        for (int f = 0; f < fc; f++) {
            out[p][f] = gravis::d2Vector(
                pos[2 * (p * fc + f) + 0], pos[2 * (p * fc + f) + 1]);
        }
    }

    return out;
}

std::vector<double> MotionRefinement::pack(
    const std::vector<std::vector<gravis::d2Vector>> &pos
) {
    const int pc = pos.size();

    if (pc == 0) return std::vector<double>(0);

    const int fc = pos[0].size();

    std::vector<double> out(2 * pc * fc);

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            out[2 * (p * fc + f) + 0] = pos[p][f].x;
            out[2 * (p * fc + f) + 1] = pos[p][f].y;
        }
    }

    return out;
}

ParticleMotionFit::ParticleMotionFit(
    const std::vector<Image<float>> &correlation, 
    RFLOAT lambda_vel, RFLOAT lambda_acc
): correlation(correlation), lambda_vel(lambda_vel), lambda_acc(lambda_acc) {}

double ParticleMotionFit::f(const std::vector<double> &x, void* tempStorage) const {
    const double cx = correlation[0]().xdim / 2;
    const double cy = correlation[0]().ydim / 2;
    const int ic = correlation.size();

    double out = 0.0;

    for (int i = 0; i < ic; i++) {
        const double xi = x[2 * i]     + cx;
        const double yi = x[2 * i + 1] + cy;

        //out -= Interpolation::linearXY(correlation[i], xi, yi, 0);
        out -= Interpolation::cubicXY(correlation[i], xi, yi, 0);

        if (i > 0) {
            const double xn = x[2 * (i - 1)]     + cx;
            const double yn = x[2 * (i - 1) + 1] + cy;

            const double dx = xi - xn;
            const double dy = yi - yn;

            out += lambda_vel * (dx * dx + dy * dy);

        }

        if (i > 0 && i < ic-1) {
            const double xp = x[2 * (i - 1)] + cx;
            const double yp = x[2 * (i - 1) + 1] + cy;
            const double xn = x[2 * (i + 1)] + cx;
            const double yn = x[2 * (i + 1) + 1] + cy;

            const double ax = xp + xn - 2.0 * xi;
            const double ay = yp + yn - 2.0 * yi;

            out += lambda_acc * (ax * ax + ay * ay);
        }
    }

    return out;
}

MotionFit::MotionFit(
    const std::vector<std::vector<Image<float>>> &correlation,
    const std::vector<std::vector<RFLOAT>> &distWeights,
    RFLOAT lambda, RFLOAT mu
): correlation(correlation), distWeights(distWeights), lambda(lambda), mu(mu) {}

double MotionFit::f(const std::vector<double> &x, void* tempStorage) const {
    const int pc = correlation.size();

    if (pc == 0) return 0.0;

    const int fc = correlation[0].size();

    if (fc == 0) return 0.0;

    const int w = correlation[0][0].data.xdim;
    const int h = correlation[0][0].data.ydim;
    const double mx = w / 2;
    const double my = h / 2;

    double e = 0.0;

    const double eps = 0.001; // cutoff at 2.62 std.dev.

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            const double xpf = x[2 * (p * fc + f) + 0];
            const double ypf = x[2 * (p * fc + f) + 1];

            e -= Interpolation::cubicXY(correlation[p][f], xpf + mx, ypf + my, 0);

            if (f > 0 && f < fc - 1) {
                const double xpfn = x[2 * (p * fc + f - 1) + 0];
                const double ypfn = x[2 * (p * fc + f - 1) + 1];

                const double xpfp = x[2 * (p * fc + f + 1) + 0];
                const double ypfp = x[2 * (p * fc + f + 1) + 1];

                const double cx = xpfn + xpfp - 2.0 * xpf;
                const double cy = ypfn + ypfp - 2.0 * ypf;

                e += lambda * (cx * cx + cy * cy);
            }

            if (f > 0) {
                const double xpfn = x[2 * (p * fc + f - 1) + 0];
                const double ypfn = x[2 * (p * fc + f - 1) + 1];

                for (int q = p+1; q < pc; q++) {
                    if (distWeights[p][q] < eps) continue;

                    const double xqf = x[2 * (q * fc + f) + 0];
                    const double yqf = x[2 * (q * fc + f) + 1];

                    const double xqfn = x[2*(q * fc + f - 1) + 0];
                    const double yqfn = x[2*(q * fc + f - 1) + 1];

                    const double cx = (xpf - xpfn) - (xqf - xqfn);
                    const double cy = (ypf - ypfn) - (yqf - yqfn);

                    e += mu * (cx * cx + cy * cy);
                }
            }
        }
    }

    return e;
}

double MotionFit::f_data(const std::vector<double> &x) const {
    const int pc = correlation.size();

    if (pc == 0) return 0.0;

    const int fc = correlation[0].size();

    if (fc == 0) return 0.0;

    const int w = correlation[0][0].data.xdim;
    const int h = correlation[0][0].data.ydim;
    const double mx = w / 2;
    const double my = h / 2;

    double e = 0.0;

    const double eps = 0.001; // cutoff at 2.62 std.dev.

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            const double xpf = x[2 * (p * fc + f) + 0];
            const double ypf = x[2 * (p * fc + f) + 1];

            e -= Interpolation::cubicXY(correlation[p][f], xpf + mx, ypf + my, 0);
        }
    }

    return e;
}

void MotionFit::grad(
    const std::vector<double> &x, std::vector<double> &gradDest, 
    void* tempStorage
) const {
    const int pc = correlation.size();

    if (pc == 0) return;

    const int fc = correlation[0].size();

    if (fc == 0) return;

    const int w = correlation[0][0].data.xdim;
    const int h = correlation[0][0].data.ydim;
    const double mx = w / 2;
    const double my = h / 2;

    const double eps = 0.001; // cutoff at 2.62 std.dev.

    for (int p = 0; p < pc; p++)
    for (int f = 0; f < fc; f++) {
        gradDest[2 * (p * fc + f) + 0] = 0.0;
        gradDest[2 * (p * fc + f) + 1] = 0.0;
    }

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            const double xpf = x[2 * (p * fc + f) + 0];
            const double ypf = x[2 * (p * fc + f) + 1];

            //e -= Interpolation::cubicXY(correlation[p][f], xpf + mx, ypf + my, 0);

            gravis::t2Vector<RFLOAT> g = Interpolation::cubicXYgrad(correlation[p][f], xpf + mx, ypf + my, 0);

            gradDest[2 * (p * fc + f) + 0] -= g.x;
            gradDest[2 * (p * fc + f) + 1] -= g.y;

            if (f > 0 && f < fc-1) {
                const double xpfn = x[2 * (p * fc + f - 1) + 0];
                const double ypfn = x[2 * (p * fc + f - 1) + 1];

                const double xpfp = x[2 * (p * fc + f + 1) + 0];
                const double ypfp = x[2 * (p * fc + f + 1) + 1];

                const double cx = xpfn + xpfp - 2.0 * xpf;
                const double cy = ypfn + ypfp - 2.0 * ypf;

                //e += lambda * (cx * cx + cy * cy);
                gradDest[2 * (p * fc + f - 1) + 0] += 2.0 * lambda * cx;
                gradDest[2 * (p * fc + f - 1) + 1] += 2.0 * lambda * cy;

                gradDest[2 * (p * fc + f) + 0] -= 4.0 * lambda * cx;
                gradDest[2 * (p * fc + f) + 1] -= 4.0 * lambda * cy;

                gradDest[2 * (p * fc + f + 1) + 0] += 2.0 * lambda * cx;
                gradDest[2 * (p * fc + f + 1) + 1] += 2.0 * lambda * cy;
            }

            if (f > 0) {
                const double xpfn = x[2 * (p * fc + f - 1) + 0];
                const double ypfn = x[2 * (p * fc + f - 1) + 1];

                for (int q = p + 1; q < pc; q++) {
                    if (distWeights[p][q] < eps) continue;

                    const double xqf = x[2 * (q * fc + f) + 0];
                    const double yqf = x[2 * (q * fc + f) + 1];

                    const double xqfn = x[2 * (q * fc + f - 1) + 0];
                    const double yqfn = x[2 * (q * fc + f - 1) + 1];

                    const double cx = (xpf - xpfn) - (xqf - xqfn);
                    const double cy = (ypf - ypfn) - (yqf - yqfn);

                    //e += mu * (cx * cx + cy * cy);
                    gradDest[2 * (p * fc + f - 1) + 0] -= 2.0 * mu * cx;
                    gradDest[2 * (p * fc + f - 1) + 1] -= 2.0 * mu * cy;

                    gradDest[2 * (p * fc + f) + 0] += 2.0 * mu * cx;
                    gradDest[2 * (p * fc + f) + 1] += 2.0 * mu * cy;

                    gradDest[2 * (q * fc + f - 1) + 0] += 2.0 * mu * cx;
                    gradDest[2 * (q * fc + f - 1) + 1] += 2.0 * mu * cy;

                    gradDest[2 * (q * fc + f) + 0] -= 2.0 * mu * cx;
                    gradDest[2 * (q * fc + f) + 1] -= 2.0 * mu * cy;

                }
            }
        }
    }
}

std::vector<std::vector<d2Vector>> MotionRefinement::readCollectivePaths(
    std::string filename
) {
    std::ifstream is(filename);

    int pc = 0, fc = 0;

    std::vector<std::vector<d2Vector>> coords(0);

    while (is.good()) {

        std::string line;
        std::getline(is, line);

        if (line.length() == 0) break;

        int delims = 0;

        for (int i = 0; i < line.length(); i++) {
            if (line[i] == '[' || line[i] == ']' || line[i] == ',') {
                line[i] = ' ';
                delims++;
            }
        }

        int fcp = delims / 3;

        if (pc == 0) {
            fc = fcp;
        } else if (fcp != fc) {
            REPORT_ERROR("insufficient number of frames for particle " + std::to_string(pc + 1));
        }

        std::istringstream iss(line);

        coords.push_back(std::vector<d2Vector>(fc));

        int f = 0;
        while (iss.good()) {
            iss >> coords[pc][f].x;
            iss >> coords[pc][f].y;
            f++;
        }

        pc++;

    }

    return coords;
}

void MotionRefinement::writeCollectivePaths(
    const std::vector<std::vector<d2Vector>> &data, std::string filename
) {
    const int pc = data.size();
    const int fc = data[0].size();

    std::ofstream ofs(filename);

    for (int p = 0; p < pc; p++) {
        for (int f = 0; f < fc; f++) {
            ofs << data[p][f] << " ";
        }

        ofs << "\n";
    }
}

std::vector<std::pair<int, std::vector<d2Vector>>> MotionRefinement::readPaths(
    std::string fn, int imgNum, int blockNum, int frameNum
) {
    std::vector<std::pair<int, std::vector<d2Vector>>> out(imgNum * blockNum);

    std::ifstream is(fn);

    for (int i = 0; i < imgNum; i++) {
        for (int j = 0; j < blockNum; j++) {
            std::string line;
            std::getline(is, line);

            int delims = 0;

            for (int k = 0; k < line.length(); k++) {
                if (line[k] == '[' || line[k] == ']' || line[k] == ',') {
                    line[k] = ' ';
                    delims++;
                }
            }

            int fcp = delims / 3;

            if (fcp < frameNum) {
                REPORT_ERROR("not enough frames in " + fn + ", line " + std::to_string(blockNum * (i + 1)));
            }

            out[blockNum * i + j] = std::make_pair(0, std::vector<d2Vector>(frameNum));

            std::istringstream iss(line);

            iss >> out[blockNum * i + j].first;

            for (int f = 0; f < frameNum; f++) {
                iss >> out[blockNum * i + j].second[f].x;
                iss >> out[blockNum * i + j].second[f].y;
            }
        }
    }

    return out;
}

std::vector<std::vector<d2Vector>> MotionRefinement::centerBlocks(
    std::string filenameFull, std::string filenameBlocks,
    int imgCount, int blockCount, int frameCount,
    std::vector<int>& particleNumbers, std::vector<int> &totalParticleNumbers
) {
    std::vector<std::pair<int, std::vector<d2Vector>>> full = readPaths(
        filenameFull, imgCount, 1, frameCount
    );

    std::vector<std::pair<int, std::vector<d2Vector>>> blocks = readPaths(
        filenameBlocks, imgCount, blockCount, frameCount
    );

    std::vector<std::vector<d2Vector>> out(imgCount * blockCount);

    for (int i = 0; i < imgCount;   i++)
    for (int j = 0; j < blockCount; j++) {
        out                 [i * blockCount + j] = std::vector<d2Vector>(frameCount);
        particleNumbers     [i * blockCount + j] = blocks[i * blockCount + j].first;
        totalParticleNumbers[i * blockCount + j] = full[i].first;

        for (int f = 0; f < frameCount; f++) {
            out[i * blockCount + j][f] = blocks[i * blockCount + j].second[f] - full[i].second[f];
        }
    }

    return out;
}
