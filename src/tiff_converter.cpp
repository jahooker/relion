/***************************************************************************
 *
 * Author: "Takanori Nakane"
 * MRC Laboratory of Molecular Biology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/

#include <cstdio>
#include <cmath>
#include <src/args.h>
#include <src/image.h>
#include <src/metadata_table.h>
#include <src/parallel.h>
#include <src/renderEER.h>
#include <src/tiff_converter.h>

/// TODO: Make less verbose
///       Lossy strategy

void TIFFConverter::usage() {
    parser.writeUsage(std::cerr);
}

void TIFFConverter::read(int argc, char **argv) {
    parser.setCommandLine(argc, argv);

    int general_section = parser.addSection("General Options");
    fn_in = parser.getOption("--i", "Input movie to be compressed (an MRC/MRCS file or a list of movies as .star or .lst)");
    fn_out = parser.getOption("--o", "Directory for output TIFF files", "./");
    only_do_unfinished = parser.checkOption("--only_do_unfinished", "Only process non-converted movies.");
    nr_threads = textToInteger(parser.getOption("--j", "Number of threads (useful only for --estimate_gain)", "1"));
    fn_gain = parser.getOption("--gain", "Estimated gain map and its reliablity map (read)", "");
    thresh_reliable = textToInteger(parser.getOption("--thresh", "Number of success needed to consider a pixel reliable", "50"));
    do_estimate = parser.checkOption("--estimate_gain", "Estimate gain");

    int eer_section = parser.addSection("EER rendering options");
    eer_grouping = textToInteger(parser.getOption("--eer_grouping", "EER grouping", "40"));
    eer_upsampling = textToInteger(parser.getOption("--eer_upsampling", "EER upsampling (1 = 4K or 2 = 8K)", "1"));
    // --eer_upsampling 3 is only for debugging. Hidden.
    if (eer_upsampling != 1 && eer_upsampling != 2 && eer_upsampling != 3)
        REPORT_ERROR("eer_upsampling must be 1, 2 or 3");
    eer_short = parser.checkOption("--short", "use unsigned short instead of signed byte for EER rendering");

    int tiff_section = parser.addSection("TIFF writing options");
    fn_compression = parser.getOption("--compression", "compression type (none, auto, deflate (= zip), lzw)", "auto");
    deflate_level = textToInteger(parser.getOption("--deflate_level", "deflate level. 1 (fast) to 9 (slowest but best compression)", "6"));
    //lossy = parser.checkOption("--lossy", "Allow slightly lossy but better compression on defect pixels");
    dont_die_on_error = parser.checkOption("--ignore_error", "Don't die on un-expected defect pixels (can be dangerous)");
    line_by_line = parser.checkOption("--line_by_line", "Use one strip per row");

    if (parser.checkForErrors())
        REPORT_ERROR("Errors encountered on the command line (see above), exiting...");
}

void TIFFConverter::estimate(FileName fn_movie) {
    Image<float> frame;
    frame.read(fn_movie, false, -1, nullptr, true); // select_img -1, mmap false, is_2D true
    if (Xsize(frame()) != Xsize(gain()) || Ysize(frame()) != Ysize(gain()))
        REPORT_ERROR("The movie " + fn_movie + " has a different size from others.");

    const int nframes = Nsize(frame());

    for (int iframe = 0; iframe < nframes; iframe++) {
        int error = 0, changed = 0, stable = 0, negative = 0;

        frame.read(fn_movie, true, iframe, nullptr, true);

        #pragma omp parallel for num_threads(nr_threads) reduction(+:error, changed, negative)
        for (long int n = 0; n < frame().size(); n++) {
            const float val = frame()[n];
            const float gain_here = gain()[n];

            if (val == 0) {
                continue;
            } else if (val < 0) {
                // #define DEBUG
                #ifdef DEBUG
                printf(" negative: %s frame %2d pos %4d %4d obs % 8.4f gain %.4f\n",
                       fn_movie.c_str(), iframe, n / Xsize(gain()), n % Xsize(gain()), (double)val, (double)gain_here);
                #endif
                negative++;
                defects()[n] = -1;
            } else if (gain_here > val) {
                gain()[n] = val;
                changed++;
                defects()[n] = 0;
            } else {
                const int ival = (int)round(val / gain_here);
                const float expected = gain_here * ival;
                if (fabs(expected - val) > 0.0001) {
                    #ifdef DEBUG
                    printf(" mismatch: %s frame %2d pos %4d %4d obs % 8.4f expected % 8.4f gain %.4f\n",
                           fn_movie.c_str(), iframe, n / Xsize(gain()), n % Xsize(gain()), (double)val,
                           (double)expected, (double)gain_here);
                    #endif
                    error++;
                    defects()[n] = -1;
                } else if (defects()[n] >= 0) {
                    defects()[n]++;
                }
            }
        }

        #pragma omp parallel for num_threads(nr_threads) reduction(+:stable)
        for (long int n = 0; n < defects().size(); n++) {
            short val = defects()[n];
            if (val >= thresh_reliable) { stable++; }
        }

        printf(
            " %s Frame %03d #Changed %10d #Mismatch %10d, #Negative %10d, #Unreliable %10d / %10d\n",
            fn_movie.c_str(), iframe + 1, changed, error, negative, Xsize(defects()) * Ysize(defects()) - stable, Xsize(defects()) * Ysize(defects())
        );
    }
}

int TIFFConverter::decide_filter(int nx, bool isEER) {
    if (fn_compression == "none") {
        return COMPRESSION_NONE;
    } else if (fn_compression == "lzw") {
        return COMPRESSION_LZW;
    } else if (fn_compression == "deflate" || fn_compression == "zip") {
        return COMPRESSION_DEFLATE;
    } else if (fn_compression == "auto") {
        if (nx == 4096 && !isEER) {
            return COMPRESSION_DEFLATE; // likely Falcon
        } else {
            return COMPRESSION_LZW;
        }
    } else {
        REPORT_ERROR("Compression type must be one of none, auto, deflate (= zip) or lzw.");
    }
    return -1;
}

template <typename T>
void TIFFConverter::unnormalise(FileName fn_movie, FileName fn_tiff) {
    FileName fn_tmp = fn_tiff + ".tmp";
    TIFF *tif = TIFFOpen(fn_tmp.c_str(), "w");
    if (!tif) REPORT_ERROR("Failed to open the output TIFF file: " + fn_tiff);

    Image<float> frame;
    char msg[256];

    frame.read(fn_movie, false, -1, nullptr, true); // select_img -1, mmap false, is_2D true
    if (Xsize(frame()) != Xsize(gain()) || Ysize(frame()) != Ysize(gain()))
        REPORT_ERROR("The movie " + fn_movie + " has a different size from others.");

    const int nframes = Nsize(frame());
    const float angpix = frame.samplingRateX();
    MultidimArray<T> buf(Ysize(frame()), Xsize(frame()));

    for (int iframe = 0; iframe < nframes; iframe++) {
        int error = 0;

        frame.read(fn_movie, true, iframe, nullptr, true);

        #pragma omp parallel for num_threads(nr_threads) reduction(+:error)
        for (long int n = 0; n < frame().size(); n++) {
            const float val       = frame()[n];
            const float gain_here = gain()[n];
            bool is_bad = defects()[n] < thresh_reliable;

            if (is_bad) {
                // TODO: implement other strategy
                buf[n] = val;
                continue;
            }

            int ival = (int)round(val / gain_here);
            const float expected = gain_here * ival;
            if (fabs(expected - val) > 0.0001) {
                snprintf(
                    msg, 255, " mismatch: %s frame %2d pos %4ld %4ld obs % 8.4f status %d expected % 8.4f gain %.4f\n",
                    fn_movie.c_str(), iframe, n / Xsize(gain()), n % Xsize(gain()), (double) val, defects()[n],
                    (double) expected, (double) gain_here
                );
                std::cerr << msg << std::endl;
                if (!dont_die_on_error)
                    REPORT_ERROR("Unexpected pixel value in a pixel that was considered reliable");
                error++;
            }

            if (!std::is_same<T, float>::value) {
                const int overflow = std::is_same<T, short>::value ? 32767: 127;
                const int underflow = std::is_same<T, short>::value ? -32768: 0;

                if (ival < underflow) {
                    ival = underflow;
                    error++;

                    printf(
                        " underflow: %s frame %2d pos %4ld %4ld obs % 8.4f expected % 8.4f gain %.4f\n",
                        fn_movie.c_str(), iframe, n / Xsize(gain()), n % Xsize(gain()), (double) val,
                        (double) expected, (double) gain_here
                    );
                } else if (ival > overflow) {
                    ival = overflow;
                    error++;

                    printf(
                        " overflow: %s frame %2d pos %4ld %4ld obs % 8.4f expected % 8.4f gain %.4f\n",
                        fn_movie.c_str(), iframe, n / Xsize(buf), n % Xsize(buf), (double) val,
                        (double) expected, (double) gain_here
                    );
                }
            }

            buf[n] = ival;
        }

        write_tiff_one_page(tif, buf, angpix, decide_filter(Xsize(buf)), deflate_level, line_by_line);

        printf(" %s Frame %3d / %3d #Error %10d\n", fn_movie.c_str(), iframe + 1, nframes, error);
    }

    TIFFClose(tif);
    std::rename(fn_tmp.c_str(), fn_tiff.c_str());
}

template <typename T>
void TIFFConverter::only_compress(FileName fn_movie, FileName fn_tiff) {
    FileName fn_tmp = fn_tiff + ".tmp";
    TIFF *tif = TIFFOpen(fn_tmp.c_str(), "w");
    if (!tif) REPORT_ERROR("Failed to open the output TIFF file.");

    if (!EERRenderer::isEER(fn_movie)) {
        Image<T> frame;
        frame.read(fn_movie, false, -1, nullptr, true); // select_img -1, mmap false, is_2D true
        const int nframes = Nsize(frame());
        const float angpix = frame.samplingRateX();

        for (int iframe = 0; iframe < nframes; iframe++) {
            frame.read(fn_movie, true, iframe, nullptr, true);
            write_tiff_one_page(tif, frame(), angpix, decide_filter(Xsize(frame())), deflate_level, line_by_line);
            printf(" %s Frame %3d / %3d\n", fn_movie.c_str(), iframe + 1, nframes);
        }
    } else {
        EERRenderer renderer;
        renderer.read(fn_movie, eer_upsampling);

        const int nframes = renderer.getNFrames();
        std::cout << " Found " << nframes << " raw frames" << std::endl;

        for (int frame = 1; frame < nframes; frame += eer_grouping) {
            const int frame_end = frame + eer_grouping - 1;
            if (frame_end > nframes)
                break;

            std::cout << " Rendering EER (hardware) frame " << frame << " to " << frame_end << std::endl;
            MultidimArray<T> buf = MultidimArray<T>::zeros(renderer.getHeight(), renderer.getWidth());
            renderer.renderFrames(frame, frame_end, buf);
            write_tiff_one_page(tif, buf, -1, decide_filter(renderer.getWidth(), true), deflate_level, line_by_line);
        }
    }

    TIFFClose(tif);
    std::rename(fn_tmp.c_str(), fn_tiff.c_str());
}

int TIFFConverter::checkMRCtype(FileName fn_movie) {
    // Check data type; Unfortunately I cannot do this through Image object.
    FILE *mrcin = fopen(fn_movie.c_str(), "r");
    int headers[25];
    fread(headers, sizeof(int), 24, mrcin);
    fclose(mrcin);

    return headers[3];
}

void TIFFConverter::initialise(int _rank, int _total_ranks) {
    rank = _rank;
    total_ranks = _total_ranks;

    if (do_estimate && total_ranks != 1)
        REPORT_ERROR("MPI parallelisation is not avaialble for --estimate_gain");

    if (fn_out[fn_out.size() - 1] != '/')
        fn_out += "/";

    FileName fn_first;

    FileName fn_in_ext = fn_in.getExtension();
    if (fn_in_ext == "star") {
        MD.read(fn_in, "movies");

        // Support non-optics group STAR files
        if (MD.empty())
            MD.read(fn_in, "");

        try {
            fn_first = MD.getValue<std::string>(EMDL::MICROGRAPH_MOVIE_NAME, 0);
        } catch (const char *errmsg) {
            REPORT_ERROR("The input STAR file does not contain the rlnMicrographMovieName column");
        }

        std::cout << "The number of movies in the input: " << MD.size() << std::endl;
    } else if (fn_in_ext == "lst") {
        // treat as a simple list
        std::ifstream f (fn_in);
        std::string line;
        while (std::getline(f, line)) {
            MD.setValue(EMDL::MICROGRAPH_MOVIE_NAME, line, MD.addObject());
        }

        fn_first = MD.getValue<std::string>(EMDL::MICROGRAPH_MOVIE_NAME, 0);
    } else {
        MD.setValue(EMDL::MICROGRAPH_MOVIE_NAME, fn_in, MD.addObject());
        fn_first = fn_in;
    }

    if (fn_first.getExtension() != "mrc" && fn_first.getExtension() != "mrcs" && !EERRenderer::isEER(fn_first))
        REPORT_ERROR(fn_first + ": the input must be MRC, MRCS or EER files");

    if (fn_out.contains("/"))
        system(("mkdir -p " + fn_out.beforeLastOf("/")).c_str());

    if (EERRenderer::isEER(fn_first)) {
        mrc_mode = -99;

        if (rank == 0) {
            if (fn_gain != "" && rank == 0) {
                EERRenderer::loadEERGain(fn_gain, gain(), eer_upsampling);
                std::cout << "Read an EER gain file " << fn_gain << " NX = " << Xsize(gain()) << " NY = " << Ysize(gain()) << std::endl;
                std::cout << "Taking inverse and re-scaling (when necessary)." << std::endl;
                gain.write(fn_out + "gain-reference.mrc");
                std::cout << "Written " + fn_out + "gain-reference.mrc. Please use this file as a gain reference when processing the converted movies.\n" << std::endl;
            } else {
                std::cerr << "WARNING: Note that an EER gain reference is the inverse of those expected for TIFF movies. You can convert your gain reference file with --gain option." << std::endl;
            }
        }

        if (do_estimate)
            REPORT_ERROR("--estimate_gain does not make sense for EER movies.");
    } else {
        if (do_estimate)
            MD.randomiseOrder();

        // Check type and mode of the input
        Image<RFLOAT> Ihead;
        Ihead.read(fn_first, false, -1, nullptr, true); // select_img -1, mmap false, is_2D true
        mrc_mode = checkMRCtype(fn_first);
        const int nx = Xsize(Ihead()), ny = Ysize(Ihead()), nn = Nsize(Ihead());
        if (rank == 0)
            printf("Input (NX, NY, NN) = (%d, %d, %d), MODE = %d\n\n", nx, ny, nn, mrc_mode);

        if (mrc_mode != 2 && do_estimate)
            REPORT_ERROR("The input movie is not in mode 2 MRC(S) file. Gain estimation does not make sense.");

        if (fn_gain != "") {
            if (mrc_mode != 2) {
                std::cerr << "The input movie is not in mode 2. A gain reference is irrelavant." << std::endl;
            } else {
                gain.read(fn_gain + ":mrc");
                if (rank == 0)
                    std::cout << "Read " << fn_gain << std::endl;
                if (Xsize(gain()) != nx || Ysize(gain()) != ny)
                    REPORT_ERROR("The input gain has a wrong size.");

                FileName fn_defects = fn_gain.withoutExtension() + "_reliablity." + fn_gain.getExtension();
                defects.read(fn_defects + ":mrc");
                if (rank == 0)
                    std::cout << "Read " << fn_defects << "\n" << std::endl;
                if (Xsize(defects()) != nx || Ysize(defects()) != ny)
                    REPORT_ERROR("The input reliability map has a wrong size.");
            }
        } else if (mrc_mode == 2) {
            gain().reshape(nx, ny);
            for (auto &x : gain()) { x = 999.9; }
            defects().reshape(nx, ny);
            for (auto &x : defects()) { x = -1; }

            if (!do_estimate)
                std::cerr << "WARNING: To effectively compress mode 2 MRC files, you should first estimate the gain with --estimate_gain." << std::endl;
        }

        if (!do_estimate && mrc_mode == 2) {
            /// TODO: other strategy
            for (long int n = 0; n < gain().size(); n++)
                if (defects()[n] < thresh_reliable) { gain()[n] = 1.0; }

            if (rank == 0 && fn_gain != "") {
                gain.write(fn_out + "gain-reference.mrc");
                std::cout << "Written " + fn_out + "gain-reference.mrc. Please use this file as a gain reference when processing the converted movies.\n" << std::endl;
            }
        }
    }
}

void TIFFConverter::processOneMovie(FileName fn_movie, FileName fn_tiff) {
    if (EERRenderer::isEER(fn_movie)) {
        if (eer_short) {
            only_compress<unsigned short>(fn_movie, fn_tiff);
        } else {
            only_compress<unsigned char>(fn_movie, fn_tiff);
        }

        return;
    }

    if (fn_movie.getExtension() != "mrc" && fn_movie.getExtension() != "mrcs") {
        std::cerr << fn_movie <<  " is not MRC, MRCS or EER file. Skipped." << std::endl;
    }

    if (mrc_mode != checkMRCtype(fn_movie))
        REPORT_ERROR("A movie " + fn_movie + " has a different mode from other movies.");

    if (mrc_mode == 1) {
        only_compress<short>(fn_movie, fn_tiff);
    } else if (mrc_mode == 6) {
        only_compress<unsigned short>(fn_movie, fn_tiff);
    } else if (mrc_mode == 0 || mrc_mode == 101) {
        only_compress<signed char>(fn_movie, fn_tiff);
    } else if (do_estimate) {
        estimate(fn_movie);

        // Write for each movie so that one can stop anytime
        gain.write(fn_out + "gain_estimate.bin:mrc"); // .bin to prevent people from using this by mistake
        defects.write(fn_out + "gain_estimate_reliablity.bin:mrc");

        std::cout << "\nUpdated " + fn_out + "gain_estimate.bin and " + fn_out + "gain_estimate_reliablity.bin\n" << std::endl;
    } else {
        unnormalise<float>(fn_movie, fn_tiff);
    }
}

void TIFFConverter::run() {
    long int my_first, my_last;
    divide_equally(MD.size(), total_ranks, rank, my_first, my_last); // MPI parallelization

    for (long i = my_first; i <= my_last; i++) {
        FileName fn_movie = MD.getValue<std::string>(EMDL::MICROGRAPH_MOVIE_NAME, i);
        FileName fn_tiff  = fn_out + fn_movie.withoutExtension() + ".tif";
        if (only_do_unfinished && !do_estimate && exists(fn_tiff)) {
            std::cout << "Skipping already processed " << fn_movie << std::endl;
            continue;
        }

        std::cout << "Processing " << fn_movie;
        if (!do_estimate)
            std::cout  << " into " << fn_tiff;
        std::cout << std::endl;

        if (fn_tiff.contains("/"))
            system(("mkdir -p " + fn_tiff.beforeLastOf("/")).c_str());

        processOneMovie(fn_movie, fn_tiff);
    }
}
