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

#include <src/jaz/stack_helper.h>
#include <src/jaz/slice_helper.h>
#include <src/projector.h>
#include <src/jaz/img_proc/filter_helper.h>
#include <src/jaz/Fourier_helper.h>
#include <src/jaz/optimization/nelder_mead.h>
#include <src/jaz/gravis/t4Matrix.h>
#include <src/jaz/image_log.h>
#include <src/fftw.h>
#include <src/micrograph_model.h>
#include <src/jaz/resampling_helper.h>
#include <src/jaz/parallel_ft.h>
#include <src/jaz/new_ft.h>

#include <omp.h>

using namespace gravis;

std::vector<MetaDataTable> StackHelper::splitByMicrographName(const MetaDataTable& mdt) {

    if (!mdt.containsLabel(EMDL::MICROGRAPH_NAME)) {
        REPORT_ERROR(
            "StackHelper::splitByMicrographName: "
            + EMDL::label2Str(EMDL::MICROGRAPH_NAME)
            + " missing from MetaDataTable.\n"
        );
    }

    MetaDataTable md2 (mdt);
    md2.newSort<MD::CompareStringsAt>(EMDL::MICROGRAPH_NAME);

    std::vector<MetaDataTable> out (0);
    const long lc = md2.size();
    std::string lastName = "";
    long curInd = -1;
    for (int i = 0; i < lc; i++) {
        std::string curName = md2.getValue<std::string>(EMDL::MICROGRAPH_NAME, i);

        if (curName != lastName) {
            lastName = curName;
            curInd++;
            out.emplace_back();
        }

        out[curInd].addObject(md2.getObject(i));
    }

    for (auto &table: out) {
        table.newSort<MD::CompareStringsBeforeAtAt>(EMDL::IMAGE_NAME);
    }

    return out;
}

MetaDataTable StackHelper::merge(const std::vector<MetaDataTable> &mdts) {
    MetaDataTable out;
    for (const auto &mdt : mdts)
        out.append(mdt);
    return out;
}

std::vector<MetaDataTable> StackHelper::splitByStack(const MetaDataTable &mdt) {

    if (!mdt.containsLabel(EMDL::IMAGE_NAME)) {
        REPORT_ERROR("StackHelper::splitByStack: " + EMDL::label2Str(EMDL::IMAGE_NAME) + " missing in meta_data_table.\n");
    }

    std::string testString = mdt.getValue<std::string>(EMDL::IMAGE_NAME, 0);

    if (testString.find("@") < 0) {
        REPORT_ERROR("StackHelper::splitByStack: " + EMDL::label2Str(EMDL::IMAGE_NAME) + " does not contain an '@'.\n");
    }

    MetaDataTable md2 (mdt);
    md2.newSort<MD::CompareStringsAfterAtAt>(EMDL::IMAGE_NAME);

    std::vector<MetaDataTable> out (0);
    const long lc = md2.size();
    std::string lastName = "";
    long curInd = -1;

    for (int i = 0; i < lc; i++) {
        std::string curFullName = md2.getValue<std::string>(EMDL::IMAGE_NAME, i);
        std::string curName = curFullName.substr(curFullName.find("@") + 1);

        if (curName != lastName) {
            lastName = curName;
            curInd++;
            out.emplace_back();
        }

        out[curInd].addObject(md2.getObject(i));
    }

    for (auto &table: out) {
        table.newSort<MD::CompareStringsBeforeAtAt>(EMDL::IMAGE_NAME);
    }

    return out;
}

std::vector<Image<RFLOAT>> StackHelper::loadStack(
    const MetaDataTable &mdt, std::string path, int threads
) {

    std::string fullName = mdt.getValue<std::string>(EMDL::IMAGE_NAME, 0);
    std::string name = fullName.substr(fullName.find("@") + 1);

    if (!path.empty()) {
        name = path + "/" + name.substr(name.find_last_of("/") + 1);
    }

    std::vector<Image<RFLOAT>> out (mdt.size());
    const long ic = mdt.size();
    #pragma omp parallel for num_threads(threads)
    for (long i = 0; i < ic; i++) {
        std::string sliceName = mdt.getValue<std::string>(EMDL::IMAGE_NAME, i);
        out[i].read(sliceName, true, -1, nullptr, true);
    }

    return out;
}

std::vector<Image<Complex>> StackHelper::loadStackFS(
    const MetaDataTable& mdt, std::string path,
    int threads, bool centerParticle, ObservationModel* obs
) {
    std::vector<Image<Complex>> out(mdt.size());

    if (centerParticle && obs == 0) {
        REPORT_ERROR("StackHelper::loadStackFS: centering particles requires an observation model.");
    }

    const long ic = mdt.size();

    std::string fullName = mdt.getValue<std::string>(EMDL::IMAGE_NAME, 0);
    std::string name = fullName.substr(fullName.find("@") + 1);

    if (!path.empty()) {
        name = path + "/" + name.substr(name.find_last_of("/") + 1);
    }

    Image<RFLOAT> dummy;
    dummy.read(name, false);

    const int s = dummy.data.xdim;

    NewFFTPlan<RFLOAT>::type plan(s, s, 1);

    #pragma omp parallel for num_threads(threads)
    for (long i = 0; i < ic; i++) {
        int optGroup = obs->getOpticsGroup(mdt, i);
        double angpix = obs->getPixelSize(optGroup);

        std::string sliceName = mdt.getValue<std::string>(EMDL::IMAGE_NAME, i);
        Image<RFLOAT> in;
        in.read(sliceName, true, -1, nullptr, true);

        NewFFT::FourierTransform(in(), out[i](), plan);

        if (centerParticle) {
            const int s = in.data.ydim;

            const double xoff = mdt.getValue<double>(EMDL::ORIENT_ORIGIN_X_ANGSTROM, i) / angpix;
            const double yoff = mdt.getValue<double>(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, i) / angpix;

            shiftImageInFourierTransform(out[i](), s, xoff - s / 2, yoff - s / 2);
        }
    }

    return out;
}

void StackHelper::saveStack(std::vector<Image<RFLOAT>> &stack, std::string fn) {
    const int w = stack[0].data.xdim;
    const int h = stack[0].data.ydim;
    const int c = stack.size();

    Image<RFLOAT> img (w, h, 1, c);
    for (int i = 0; i < c; i++) {
        SliceHelper::insertStackSlice(stack[i], img, i);
    }
    img.write(fn);
}

std::vector<std::vector<Image<RFLOAT>>> StackHelper::loadMovieStack(
    const MetaDataTable &mdt, const std::string &moviePath
) {

    std::string fullName  = mdt.getValue<std::string>(EMDL::IMAGE_NAME, 0);
    std::string movieName = mdt.getValue<std::string>(EMDL::MICROGRAPH_NAME, 0);
    std::string name = fullName.substr(fullName.find("@") + 1);

    std::string finName = moviePath.empty() ?
        name : moviePath + "/" + movieName.substr(movieName.find_last_of("/") + 1);

    std::cout << "loading real: " << finName << "\n";

    const auto img = Image<RFLOAT>::from_filename(finName);

    const long pc = mdt.size();
    std::cout << "size = " << img.data.xdim << "x" << img.data.ydim << "x" << img.data.zdim << "x" << img.data.ndim << "\n";
    std::cout << "pc = " << pc << "\n";

    const int fc = img.data.ndim / pc;

    using Movie = std::vector<Image<RFLOAT>>;
    std::vector<Movie> stack;
    for (long p = 0; p < pc; p++) {
        Movie movie;
        for (long f = 0; f < fc; f++) {
            movie.push_back(SliceHelper::getStackSlice(img, f * pc + p));
        }
        stack.push_back(std::move(movie));
    }
    return stack;
}

void fix_defect(
    Image<float> &muGraph, MultidimArray<bool> &defectMask,
    int w0, int h0, int threads_p
) {

    RFLOAT frame_mean = 0, frame_std = 0;
    long long n_valid = 0;

    #pragma omp parallel for reduction(+:frame_mean, n_valid) num_threads(threads_p)
    for (long int n = 0; n < muGraph.data.size(); n++) {
        if (!defectMask[n]) continue;
        frame_mean += muGraph.data[n];
        n_valid++;
    }
    frame_mean /= n_valid;

    #pragma omp parallel for reduction(+:frame_std) num_threads(threads_p)
    for (long int n = 0; n < muGraph.data.size(); n++) {
        if (!defectMask[n]) continue;
        RFLOAT d = muGraph.data[n] - frame_mean;
        frame_std += d * d;
    }
    frame_std = std::sqrt(frame_std / n_valid);

    // 25 neighbours; should be enough even for super-resolution images.
    const int NUM_MIN_OK = 6;
    const int D_MAX = 2; // EER code path does not use this function
    const int PBUF_SIZE = 100;
    #pragma omp parallel for num_threads(threads_p)
    for (long int j = 0; j < Ysize(muGraph.data); j++)
    for (long int i = 0; i < Xsize(muGraph.data); i++) {
        if (!direct::elem(defectMask, i, j)) continue;

        int n_ok = 0;
        RFLOAT pbuf[PBUF_SIZE];
        for (int dy = -D_MAX; dy <= D_MAX; dy++) {
            int y = j + dy;
            if (y < 0 || y >= h0) continue;
            for (int dx = -D_MAX; dx <= D_MAX; dx++) {
                int x = i + dx;
                if (x < 0 || x >= w0) continue;
                if (direct::elem(defectMask, x, y)) continue;

                pbuf[n_ok] = direct::elem(muGraph.data, x, y);
                n_ok++;
            }
        }
        direct::elem(muGraph.data, i, j) = n_ok > NUM_MIN_OK ?
            pbuf[rand() % n_ok] : rnd_gaus(frame_mean, frame_std);
    }
}

std::vector<std::vector<Image<Complex>>> StackHelper::extractMovieStackFS(
    const MetaDataTable &mdt,
    Image<RFLOAT>* gainRef, MultidimArray<bool>* defectMask, std::string movieFn,
    double outPs, double coordsPs, double moviePs, double dataPs,
    int squareSize, int threads, // squareSize is the output box size in pixels after downsampling to outPs
    bool loadData, int firstFrame, int lastFrame,
    RFLOAT hot, bool verbose, bool saveMemory,
    const std::vector<std::vector<gravis::d2Vector>>* offsets_in,
    std::vector<std::vector<gravis::d2Vector>>* offsets_out
) {
    const long pc = mdt.size();

    Image<float> mgStack;
    mgStack.read(movieFn, false);

    if (verbose) {
        std::cout 
            << "size: "
            << mgStack().xdim << "×"
            << mgStack().ydim << "×"
            << mgStack().zdim << "×"
            << mgStack().ndim << "\n";
    }

    const bool dataInZ = mgStack.data.zdim > 1;

    const int w0 = mgStack.data.xdim;
    const int h0 = mgStack.data.ydim;
    const int fcM = dataInZ ? mgStack.data.zdim : mgStack.data.ndim;
    // lastFrame and firstFrame is 0 indexed, while fcM is 1-indexed
    const int fc = lastFrame > 0 ? lastFrame - firstFrame + 1 : fcM - firstFrame;

    if (dataPs < 0) dataPs = outPs;

    if (fcM <= lastFrame) {
        REPORT_ERROR("StackHelper::extractMovieStackFS: insufficient number of frames in "+movieFn);
    }

    const bool useGain = gainRef != 0;
    if (useGain && (w0 != gainRef->data.xdim || h0 != gainRef->data.ydim)) {
        REPORT_ERROR("StackHelper::extractMovieStackFS: incompatible gain reference - size is different from "+movieFn);
    }

    const bool fixDefect = false; // TAKANORI DEBUG: defectMask != 0;
    if (fixDefect && (w0 != defectMask->xdim || h0 != defectMask->ydim)) {
        REPORT_ERROR("StackHelper::extractMovieStackFS: incompatible defect mask - size is different from "+movieFn);
    }

    if (verbose) {
        std::cout << (dataInZ ? "data in Z\n" : "data in N\n");
        std::cout << "frame count in movie = " << fcM << "\n";
        std::cout << "frame count to load  = " << fc << "\n";
        std::cout << "pc, fc = " << pc << ", " << fc << "\n";
    }

    using T = std::vector<Image<Complex>>;
    std::vector<T> out (pc, T(fc));

    if (!loadData) return out;

    const int sqMg = 2 * (int) (0.5 * squareSize * outPs / moviePs + 0.5);
    // This should be equal to s_mov in frame_recombiner

    if (verbose) {
        std::cout << "square size in micrograph: " << sqMg << "\n";
    }

    std::vector<ParFourierTransformer> fts (threads);

    using R = Image<RFLOAT>;
    using C = Image<Complex>;
    std::vector<R> raux (threads, {sqMg, sqMg});
    std::vector<C> caux (threads, outPs == moviePs ? C() : C(sqMg / 2 + 1, sqMg));

    int threads_f = saveMemory ? 1 : threads;
    int threads_p = saveMemory ? threads : 1;

    #pragma omp parallel for num_threads(threads_f)
    for (long f = 0; f < fc; f++) {
        int tf = omp_get_thread_num();

        Image<float> muGraph;
        muGraph.read(movieFn, true, f + firstFrame, nullptr, true);

        if (verbose) { std::cout << f + 1 << "/" << fc << "\n"; }

        #pragma omp parallel for num_threads(threads_p)
        for (long int y = 0; y < h0; y++)
        for (long int x = 0; x < w0; x++) {

            const RFLOAT gain = useGain ? direct::elem(gainRef->data, x, y) : 1.0;
            RFLOAT val = direct::elem(muGraph.data, x, y);
            if (0.0 < hot && hot < val) { val = hot; }

            direct::elem(muGraph.data, x, y) = -gain * val;
        }

        if (fixDefect) fix_defect(muGraph, *defectMask, w0, h0, threads_p);

        /// TODO: TAKANORI: Cache muGraph HERE

        #pragma omp parallel for num_threads(threads_p)
        for (long p = 0; p < pc; p++) {
            int tp = omp_get_thread_num();

            int t = saveMemory ? tp : tf;

            out[p][f] = Image<Complex>(sqMg, sqMg);

            double xpC = mdt.getValue<double>(EMDL::IMAGE_COORD_X, p);
            double ypC = mdt.getValue<double>(EMDL::IMAGE_COORD_Y, p);

            const double xpO = (int) (coordsPs * xpC / dataPs);
            const double ypO = (int) (coordsPs * ypC / dataPs);

            int x0 = round(xpO * dataPs / moviePs) - sqMg / 2;
            int y0 = round(ypO * dataPs / moviePs) - sqMg / 2;

            if (offsets_in != 0 && offsets_out != 0) {
                double dxM = (*offsets_in)[p][f].x * outPs / moviePs;
                double dyM = (*offsets_in)[p][f].y * outPs / moviePs;

                int dxI = round(dxM);
                int dyI = round(dyM);

                x0 += dxI;
                y0 += dyI;

                double dxR = (dxM - dxI) * moviePs / outPs;
                double dyR = (dyM - dyI) * moviePs / outPs;

                (*offsets_out)[p][f] = d2Vector(dxR, dyR);
            }

            for (long int y = 0; y < sqMg; y++)
            for (long int x = 0; x < sqMg; x++) {

                int xx = x0 + x;
                if (xx < 0) { xx = 0; } else if (xx >= w0) { xx = w0 - 1; }

                int yy = y0 + y;
                if (yy < 0) { yy = 0; } else if (yy >= h0) { yy = h0 - 1; }

                direct::elem(raux[t].data, x, y) = direct::elem(muGraph.data, xx, yy);
            }

            if (outPs == moviePs) {
                out[p][f].data = fts[t].FourierTransform(raux[t].data);
            } else {
                caux[t].data = fts[t].FourierTransform(raux[t].data);
                out[p][f] = FilterHelper::cropCorner2D_fftw(caux[t], squareSize / 2 + 1, squareSize);
            }

            out[p][f].data.elem(0, 0) = Complex(0.0, 0.0);
        }
    }

    return out;
}

/// TODO: TAKANORI Code duplication with above will be sorted out later!
std::vector<std::vector<Image<Complex>>> StackHelper::extractMovieStackFS(
    const MetaDataTable &mdt, std::vector<MultidimArray<float>> &Iframes,
    double outPs, double coordsPs, double moviePs, double dataPs,
    int squareSize, int threads,
    bool loadData,
    bool verbose,
    const std::vector<std::vector<gravis::d2Vector>>* offsets_in,
    std::vector<std::vector<gravis::d2Vector>>* offsets_out
) {

    const long pc = mdt.size();
    const int fc = Iframes.size();
    if (fc == 0)
        REPORT_ERROR("Empty Iframes passed to StackHelper::extractMovieStackFS");
    const int w0 = Iframes[0].xdim;
    const int h0 = Iframes[0].ydim;

    if (dataPs < 0) { dataPs = outPs; }

    if (verbose) {
        std::cout << "pc, fc = " << pc << ", " << fc << "\n";
        std::cout << "size: x = " << w0 << " y = " << h0 << "\n";
    }

    using T = std::vector<Image<Complex>>;
    std::vector<T> out (pc, T(fc));

    if (!loadData) return out;

    const int sqMg = 2 * (int) (0.5 * squareSize * outPs / moviePs + 0.5);
    // This should be equal to s_mov in frame_recombiner

    if (verbose) {
        std::cout << "square size in micrograph: " << sqMg << "\n";
    }

    std::vector<ParFourierTransformer> fts (threads);

    using R = Image<RFLOAT>;
    using C = Image<Complex>;
    std::vector<R> raux (threads, {sqMg, sqMg});
    std::vector<C> caux (threads, outPs == moviePs ? C{} : C{sqMg / 2 + 1, sqMg});

    #pragma omp parallel for num_threads(threads)
    for (long f = 0; f < fc; f++) {
        int tf = omp_get_thread_num();

        if (verbose) { std::cout << f + 1 << "/" << fc << "\n"; }

        for (long p = 0; p < pc; p++) {
            int t = tf;

            out[p][f] = Image<Complex>(sqMg, sqMg);

            double xpC = mdt.getValue<double>(EMDL::IMAGE_COORD_X, p);
            double ypC = mdt.getValue<double>(EMDL::IMAGE_COORD_Y, p);

            const double xpO = (int) (coordsPs * xpC / dataPs);
            const double ypO = (int) (coordsPs * ypC / dataPs);

            int x0 = (int) round(xpO * dataPs / moviePs) - sqMg / 2;
            int y0 = (int) round(ypO * dataPs / moviePs) - sqMg / 2;

            if (offsets_in != 0 && offsets_out != 0) {
                double dxM = (*offsets_in)[p][f].x * outPs / moviePs;
                double dyM = (*offsets_in)[p][f].y * outPs / moviePs;

                int dxI = round(dxM);
                int dyI = round(dyM);

                x0 += dxI;
                y0 += dyI;

                double dxR = (dxM - dxI) * moviePs / outPs;
                double dyR = (dyM - dyI) * moviePs / outPs;

                (*offsets_out)[p][f] = d2Vector(dxR, dyR);
            }

            for (long int y = 0; y < sqMg; y++)
            for (long int x = 0; x < sqMg; x++) {
                int xx = x0 + x;
                int yy = y0 + y;

                if (xx < 0) { xx = 0; } else if (xx >= w0) { xx = w0 - 1; }

                if (yy < 0) { yy = 0; } else if (yy >= h0) { yy = h0 - 1; }

                // Note the MINUS here!!!
                direct::elem(raux[t].data, x, y) = -direct::elem(Iframes[f], xx, yy);
            }

            if (outPs == moviePs) {
                out[p][f].data = fts[t].FourierTransform(raux[t].data);
            } else {
                caux[t].data = fts[t].FourierTransform(raux[t].data);
                out[p][f] = FilterHelper::cropCorner2D_fftw(caux[t], squareSize / 2 + 1, squareSize);
            }

            out[p][f].data.elem(0, 0) = Complex(0.0, 0.0);
        }
    }

    return out;
}

std::vector<Image<Complex>> StackHelper::FourierTransform(
    const std::vector<Image<RFLOAT>> &stack
) {
    std::vector<Image<Complex>> out;
    out.reserve(stack.size());
    FourierTransformer transformer;
    for (const auto &image : stack) {
        out.emplace_back(transformer.FourierTransform(image.data));
    }
    return out;
}

std::vector<Image<RFLOAT>> StackHelper::inverseFourierTransform(
    const std::vector<Image<Complex>> &stack
) {
    std::vector<Image<RFLOAT>> out;
    out.reserve(stack.size());
    FourierTransformer transformer;
    for (const auto &image : stack) {
        out.emplace_back(transformer.inverseFourierTransform(image.data));
    }
    return out;
}

Image<RFLOAT> StackHelper::toSingleImage(const std::vector<Image<RFLOAT>> &stack) {

    const int s = stack.size();
    if (s < 1) return Image<RFLOAT>(0, 0, 0);

    const int w = stack[0].data.xdim;
    const int h = stack[0].data.ydim;

    Image<RFLOAT> out (w, h, 1, s);

    for (int n = 0; n < s; n++) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            direct::elem(out.data, x, y, 0, n) = stack[n](y, x);
        }
    }

    return out;
}

void StackHelper::varianceNormalize(
    std::vector<Image<Complex>> &movie, bool circleCropped
) {
    const int fc = movie.size();
    const int w = movie[0].data.xdim;
    const int h = movie[0].data.ydim;
    const int wt = 2 * (w - 1);

    double var = 0.0;
    double cnt = 0.0;

    const double rr = (w - 2) * (w - 2);

    for (const auto &frame: movie) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (x == 0 && y == 0) continue;

            if (circleCropped) {
                const double yy = y < w ? y : y - h;
                const double xx = x;

                if (hypot2(xx, yy) > rr) continue;
            }

            double scale = x > 0 ? 2.0 : 1.0;

            var += scale * frame(y, x).norm();
            cnt += scale;
        }
    }

    const double scale = sqrt(wt * h * var / (cnt * fc));

    // std::cout << "scale: " << scale << "\n";

    for (auto &frame: movie) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            frame.data.elem(y, x) /= scale;
        }
    }
}

std::vector<double> StackHelper::powerSpectrum(
    const std::vector<std::vector<Image<Complex>>> &stack
) {
    const int w = stack[0][0].data.xdim;
    const int h = stack[0][0].data.ydim;

    std::vector<double> out (w, 0.0), wgh (w, 0.0);

    for (const auto &substack: stack)
    for (const auto &img: substack) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const Complex z = direct::elem(img.data, x, y);

            const double yy = y < w ? y : y - h;
            const double xx = x;

            const int r = hypot(xx, yy);

            if (r >= w) continue;

            out[r] += z.norm();
            wgh[r] += 1.0;
        }
    }

    for (int x = 0; x < w; x++) {
        if (wgh[x] > 0.0) {
            out[x] /= wgh[x];
        }
    }

    return out;
}

std::vector<double> StackHelper::varSpectrum(
    const std::vector<std::vector<Image<Complex>>> &stack
) {
    const int w = stack[0][0].data.xdim;
    const int h = stack[0][0].data.ydim;

    std::vector<double> out (w, 0.0), wgh (w, 0.0);
    std::vector<Complex> mean (w, 0.0);

    for (const auto &substack: stack)
    for (const auto &img: substack) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const Complex z = direct::elem(img.data, x, y);

            const double yy = y < w ? y : y - h;
            const double xx = x;

            const int r = hypot(xx, yy);

            if (r >= w) continue;

            mean[r] += z;
            wgh[r] += 1.0;
        }
    }

    for (int x = 0; x < w; x++) {
        if (wgh[x] > 0.0) {
            mean[x] /= wgh[x];
        }
    }

    for (const auto &substack: stack)
    for (const auto &img: substack) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const Complex z = direct::elem(img.data, x, y);

            const double yy = y < w ? y : y - h;
            const double xx = x;

            const int r = hypot(xx, yy);

            if (r >= w) continue;

            out[r] += (z - mean[r]).norm();
        }
    }

    for (int x = 0; x < w; x++) {
        if (wgh[x] > 1.0) {
            out[x] /= (wgh[x] - 1.0);
        }
    }

    return out;
}

std::vector<double> StackHelper::powerSpectrum(
        const std::vector<std::vector<Image<Complex>>>& obs,
        const std::vector<Image<Complex>>& signal
    ) {
    const int w = obs[0][0].data.xdim;
    const int h = obs[0][0].data.ydim;

    std::vector<double> out (w, 0.0), wgh (w, 0.0);

    const int ic = obs.size();
    const int fc = obs[0].size();
    for (int i = 0; i < ic; i++)
    for (const auto &img: obs[i]) {
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const Complex z = direct::elem(img.data, x, y) - direct::elem(signal[i].data, x, y);

            const double yy = y < w ? y : y - h;
            const double xx = x;

            const int r = hypot(xx, yy);

            if (r >= w) continue;

            out[r] += z.norm();
            wgh[r] += 1.0;
        }
    }

    for (int x = 0; x < w; x++) {
        if (wgh[x] > 0.0) {
            out[x] /= wgh[x];
        }
    }

    return out;
}
