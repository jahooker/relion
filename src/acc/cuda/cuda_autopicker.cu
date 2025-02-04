#undef ALTCPU
#include <sys/time.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <ctime>
#include <iostream>
#include <fstream>
#include <cuda_runtime.h>
#include <signal.h>

#include "src/multidim_array_statistics.h"
#include "src/ml_optimiser.h"
#include "src/jaz/ctf_helper.h"
#include "src/acc/acc_ptr.h"
#include "src/acc/acc_projector.h"
#include "src/acc/acc_backprojector.h"
#include "src/acc/acc_projector_plan.h"
#include "src/acc/cuda/cuda_kernels/helper.cuh"
#include "src/acc/cuda/cuda_mem_utils.h"
#include "src/acc/cuda/cuda_settings.h"
#include "src/acc/cuda/cuda_benchmark_utils.h"
#include "src/acc/cuda/cuda_fft.h"

#include "src/macros.h"
#include "src/error.h"

#ifdef CUDA_FORCESTL
#include "src/acc/cuda/cuda_utils_stl.cuh"
#else
#include "src/acc/cuda/cuda_utils_cub.cuh"
#endif

#include "src/acc/utilities.h"
#include "src/acc/acc_helper_functions.h"

#include "src/acc/cuda/cuda_autopicker.h"

// Z-score of x given mean mu and standard deviation sigma
inline RFLOAT Z(RFLOAT x, RFLOAT mu, RFLOAT sigma) {
    return (x - mu) / sigma;
}

AutoPickerCuda::AutoPickerCuda(
    AutoPicker *basePicker, int dev_id, const char * timing_fnm
):
    node(nullptr),
    basePckr(basePicker),
    allocator(new CudaCustomAllocator(0, 1)),
    micTransformer(0, allocator),
    cudaTransformer1(0, allocator),
    #ifdef TIMING_FILES
    timer(timing_fnm),
    #endif
    cudaTransformer2(0, allocator)
{
    projectors.resize(basePckr->Mrefs.size());
    have_warned_batching = false;
    /*======================================================
                        DEVICE SETTINGS
    ======================================================*/
    device_id = dev_id;
    int devCount;
    HANDLE_ERROR(cudaGetDeviceCount(&devCount));

    if (dev_id >= devCount) {
        // std::cerr << " using device_id=" << dev_id << " (device no. " << dev_id + 1 << ") which is higher than the available number of devices=" << devCount << std::endl;
        CRITICAL(ERR_GPUID);
    } else {
        HANDLE_ERROR(cudaSetDevice(dev_id));
    }
};

AutoPickerCuda::AutoPickerCuda(AutoPickerMpi *basePicker, int dev_id, const char *timing_fnm):
    basePckr(basePicker),
    allocator(new CudaCustomAllocator(0, 1)),
    micTransformer(0, allocator),
    cudaTransformer1(0, allocator),
    #ifdef TIMING_FILES
    timer(timing_fnm),
    #endif
    cudaTransformer2(0, allocator)
{
    node = basePicker->getNode();
    basePicker->verb = node->isLeader();

    projectors.resize(basePckr->Mrefs.size());
    have_warned_batching = false;
    /*======================================================
                        DEVICE SETTINGS
    ======================================================*/
    device_id = dev_id;
    int devCount;
    HANDLE_ERROR(cudaGetDeviceCount(&devCount));

    if (dev_id >= devCount) {
        // std::cerr << " using device_id=" << dev_id << " (device no. " << dev_id + 1 << ") which is higher than the available number of devices=" << devCount << std::endl;
        CRITICAL(ERR_GPUID);
    } else {
        HANDLE_ERROR(cudaSetDevice(dev_id));
    }
};

void AutoPickerCuda::run() {
    long int my_first_micrograph, my_last_micrograph, my_nr_micrographs;
    if (node) {
        // Each node does part of the work
        divide_equally(basePckr->fn_micrographs.size(), node->size, node->rank, my_first_micrograph, my_last_micrograph);
    } else {
        my_first_micrograph = 0;
        my_last_micrograph = basePckr->fn_micrographs.size() - 1;
    }
    my_nr_micrographs = my_last_micrograph - my_first_micrograph + 1;

    int barstep;
    if (basePckr->verb > 0) {
        std::cout << " Autopicking ..." << std::endl;
        init_progress_bar(my_nr_micrographs);
        barstep = std::max(1, (int) my_nr_micrographs / 60);
    }

    if (!basePckr->do_read_fom_maps) {
        CTICTOC(timer, "setupProjectors", ({
        for (int i = 0; i < basePckr->Mrefs.size(); i++) {
            auto& pp = basePckr->PPref[i];
            projectors[i].setMdlDim(
                pp.data.xdim, pp.data.ydim, pp.data.zdim,
                pp.data.yinit, pp.data.zinit,
                pp.r_max, pp.padding_factor
            );
            projectors[i].initMdl(pp.data.data);
        }
        }));
    }

    FileName fn_olddir = "";

    for (long int imic = my_first_micrograph; imic <= my_last_micrograph; imic++) {
        if (basePckr->verb > 0 && imic % barstep == 0)
            progress_bar(imic);

        // Check new-style outputdirectory exists and make it if not!
        FileName fn_dir = basePckr->getOutputRootName(basePckr->fn_micrographs[imic]);
        fn_dir = fn_dir.beforeLastOf("/");
        if (fn_dir != fn_olddir) {
            // Make a Particles directory
            system(("mkdir -p " + fn_dir).c_str());
            fn_olddir = fn_dir;
        }
        #ifdef TIMING
        basePckr->timer.tic(basePckr->TIMING_A5);
        #endif
        autoPickOneMicrograph(basePckr->fn_micrographs[imic], imic);
        #ifdef TIMING
        basePckr->timer.toc(basePckr->TIMING_A5);
        #endif
    }
    if (basePckr->verb > 0)
        progress_bar(my_nr_micrographs);

    cudaDeviceReset();
}

void AutoPickerCuda::calculateStddevAndMeanUnderMask(
    AccPtr<acc::Complex> &d_Fmic,
    AccPtr<acc::Complex> &d_Fmic2,
    AccPtr<acc::Complex> &d_Fmsk,
    int nr_nonzero_pixels_mask,
    AccPtr<XFLOAT> &d_Mstddev,
    AccPtr<XFLOAT> &d_Mmean,
    size_t x, size_t y, size_t mic_size, size_t workSize
) {
    cudaTransformer2.setSize(workSize, workSize, 1);

    deviceInitValue(d_Mstddev, (XFLOAT) 0.0);

    RFLOAT normfft = (RFLOAT) (mic_size * mic_size) / (RFLOAT) nr_nonzero_pixels_mask;

    AccPtr<acc::Complex> d_Fcov = d_Fmic.make<acc::Complex>();
    d_Fcov.setSize(d_Fmic.getSize());
    d_Fcov.deviceAlloc();

    CTICTOC(timer, "PRE-multi_0", ({
    int Bsize = ceilf((float) d_Fmic.getSize() / (float) BLOCK_SIZE);
    cuda_kernel_convol_B<<<Bsize, BLOCK_SIZE>>>(
        d_Fmic.getAccPtr(), d_Fmsk.getAccPtr(), d_Fcov.getAccPtr(), d_Fmic.getSize()
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
    }));

    CTICTOC(timer, "PRE-window_0", ({
    windowFourierTransform2(
        d_Fcov,
        cudaTransformer2.fouriers,
        x, y, 1,
        workSize / 2 + 1, workSize, 1
    );
    }));

    CTICTOC(timer, "PRE-Transform_0", ({
        cudaTransformer2.backward();
    }));

    int Bsize = ceilf((float) cudaTransformer2.reals.getSize() / (float) BLOCK_SIZE);
    cuda_kernel_multi<XFLOAT><<<Bsize, BLOCK_SIZE>>>(
        cudaTransformer2.reals.getAccPtr(),
        cudaTransformer2.reals.getAccPtr(),
        (XFLOAT) normfft,
        cudaTransformer2.reals.getSize()
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());

    CTICTOC(timer, "PRE-multi_1", ({
    cuda_kernel_multi<XFLOAT><<<Bsize, BLOCK_SIZE>>>(
        cudaTransformer2.reals.getAccPtr(), cudaTransformer2.reals.getAccPtr(),
        d_Mstddev.getAccPtr(), (XFLOAT) -1, cudaTransformer2.reals.getSize());
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
    }));

    CTICTOC(timer, "PRE-CenterFFT_0", ({
    runCenterFFT(
        cudaTransformer2.reals, (int) cudaTransformer2.sizer[0], (int) cudaTransformer2.sizer[1],
        false, 1
    );
    }));

    /// TODO: remove the need for this
    cudaTransformer2.reals.cpOnAcc(d_Mmean.getDevicePtr());

    CTICTOC(timer, "PRE-multi_2", ({
    Bsize = ((int) ceilf((float) d_Fmsk.getSize() / (float) BLOCK_SIZE));
    cuda_kernel_convol_A<<<Bsize, BLOCK_SIZE>>>(
        d_Fmsk.getAccPtr(), d_Fmic2.getAccPtr(), d_Fcov.getAccPtr(), d_Fmsk.getSize()
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
    }));

    CTICTOC(timer, "PRE-window_1", ({
    windowFourierTransform2(
        d_Fcov, cudaTransformer2.fouriers, x, y, 1, workSize / 2 + 1, workSize, 1
    );
    }));

    CTICTOC(timer, "PRE-Transform_1", ({
    cudaTransformer2.backward();
    }));

    CTICTOC(timer, "PRE-multi_3", ({
    Bsize = ((int) ceilf((float) d_Mstddev.getSize() / (float) BLOCK_SIZE));
    cuda_kernel_finalizeMstddev<<<Bsize, BLOCK_SIZE>>>(
        d_Mstddev.getAccPtr(), cudaTransformer2.reals.getAccPtr(), normfft, d_Mstddev.getSize()
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
    }));

    CTICTOC(timer, "PRE-CenterFFT_1", ({
    runCenterFFT(
        d_Mstddev, (int) workSize, (int) workSize, false, 1
    );
    }));
}

void AutoPickerCuda::autoPickOneMicrograph(FileName &fn_mic, long int imic) {
    Image<RFLOAT> Imic;
    MultidimArray<Complex> Faux, Faux2, Fmic;
    MultidimArray<RFLOAT> Maux, Mstddev, Mmean, Mstddev2, Mavg, Mccf_best, Mpsi_best, Fctf, Mccf_best_combined, Mpsi_best_combined;
    MultidimArray<int> Mclass_best_combined;

    AccPtr<XFLOAT> d_Mccf_best(basePckr->workSize * basePckr->workSize, allocator);
    AccPtr<XFLOAT> d_Mpsi_best(basePckr->workSize * basePckr->workSize, allocator);
    d_Mccf_best.deviceAlloc();
    d_Mpsi_best.deviceAlloc();

    // Always use the same random seed
    init_random_generator(basePckr->random_seed + imic);

    RFLOAT sum_ref_under_circ_mask, sum_ref2_under_circ_mask;
    int my_skip_side = basePckr->autopick_skip_side + basePckr->particle_size / 2;

    int Npsi = 360 / basePckr->psi_sampling;

    int min_distance_pix = round(basePckr->min_particle_distance / basePckr->angpix);
    XFLOAT scale = (XFLOAT)basePckr->workSize / (XFLOAT)basePckr->micrograph_size;

    // Read in the micrograph
    #ifdef TIMING
    basePckr->timer.tic(basePckr->TIMING_A6);
    #endif
    CTICTOC(timer, "readMicrograph", {
    Imic.read(fn_mic);
    });
    CTICTOC(timer, "setXmippOrigin_0", {
    Imic().setXmippOrigin();
    });
    #ifdef TIMING
    basePckr->timer.toc(basePckr->TIMING_A6);
    #endif

    // Let's just check the square size again....
    RFLOAT my_size, my_xsize, my_ysize;
    my_xsize = Xsize(Imic());
    my_ysize = Ysize(Imic());
    my_size = std::max(my_xsize, my_ysize);
    if (basePckr->extra_padding > 0)
        my_size += 2 * basePckr->extra_padding;

    if (my_size != basePckr->micrograph_size || my_xsize != basePckr->micrograph_xsize || my_ysize != basePckr->micrograph_ysize) {
        Imic().printShape();
        std::cerr
        << " micrograph_size= "  << basePckr->micrograph_size
        << " micrograph_xsize= " << basePckr->micrograph_xsize
        << " micrograph_ysize= " << basePckr->micrograph_ysize
        << std::endl;
        REPORT_ERROR("AutoPicker::autoPickOneMicrograph ERROR: No differently sized micrographs are allowed in one run, sorry you will have to run separately for each size...");
    }

    if (!basePckr->do_read_fom_maps) {
        CTICTOC(timer, "setSize_micTr", {
        micTransformer.setSize(basePckr->micrograph_size, basePckr->micrograph_size, 1, 1);
        });

        CTICTOC(timer, "setSize_cudaTr", {
        cudaTransformer1.setSize(basePckr->workSize,basePckr->workSize, 1, Npsi, FFTW_BACKWARD);
        });
    }
    HANDLE_ERROR(cudaDeviceSynchronize());

    if (cudaTransformer1.batchSize.size() > 1 && !have_warned_batching) {
        have_warned_batching = true;
        std::cerr << "\n";
        std::cerr << "*-----------------------------WARNING------------------------------------------------*\n";
        std::cerr << "With the current settings, the GPU memory is imposing a soft limit on your performace,\n";
        std::cerr << "since at least one micrograph needs at least " << cudaTransformer1.batchSize.size() << " batches of orientations \n";
        std::cerr << "to achieve the total requested " << Npsi << " orientations. Consider using\n";
        std::cerr << "\t higher --ang\n";
        std::cerr << "\t harder --shrink\n";
        std::cerr << "\t higher --lowpass with --shrink 0\n";
        std::cerr << "*------------------------------------------------------------------------------------*"<< std::endl;
    }

    // Set mean to zero and stddev to 1
    // to prevent numerical problems with single-pass stddev calculations.
    #ifdef TIMING
    basePckr->timer.tic(basePckr->TIMING_A7);
    #endif
    const auto stats = [&] () -> Stats<RFLOAT> {
        CTICTOC(timer, "computeStats", ({ return computeStats(Imic()); }));
    }();
    #ifdef TIMING
    basePckr->timer.toc(basePckr->TIMING_A7);
    #endif
    CTICTOC(timer, "middlePassFilter", ({
    for (long int n = 0; n < Imic().size(); n++) {
        // Remove pixel values that are too far away from the mean
        if (abs(Z(Imic()[n], stats.avg, stats.stddev)) > basePckr->outlier_removal_zscore)
            Imic()[n] = stats.avg;

        Imic()[n] = Z(Imic()[n], stats.avg, stats.stddev);
    }
    }));

    if (
        basePckr->micrograph_xsize != basePckr->micrograph_size ||
        basePckr->micrograph_ysize != basePckr->micrograph_size
    ) {
        CTICTOC(timer, "rewindow", ({
        // Window non-square micrographs to be a square with the largest side
        rewindow(Imic, basePckr->micrograph_size);
        }));
        CTICTOC(timer, "gaussNoiseOutside", ({
        // Fill region outside the original window with white Gaussian noise to prevent all-zeros in Mstddev
        FOR_ALL_ELEMENTS_IN_ARRAY2D(Imic(), i, j) {
            if (
                i < Xmipp::init(basePckr->micrograph_xsize) || i > Xmipp::last(basePckr->micrograph_xsize) ||
                j < Xmipp::init(basePckr->micrograph_ysize) || j > Xmipp::last(basePckr->micrograph_ysize)
            ) {
                Imic().elem(i, j) = rnd_gaus(0.0, 1.0);
            }
        }
        }));
    }


    #ifdef TIMING
    basePckr->timer.tic(basePckr->TIMING_A8);
    #endif
    CTICTOC(timer, "CTFread", ({
    // Read in the CTF information if needed
    if (basePckr->do_ctf) {
        // Search for this micrograph in the metadata table
        for (long int i : basePckr->MDmic) {
            const FileName fn_tmp = basePckr->MDmic.getValue<std::string>(EMDL::MICROGRAPH_NAME, i);
            if (fn_tmp == fn_mic) {
                CTF ctf = CtfHelper::makeCTF(basePckr->MDmic, &basePckr->obsModel);
                Fctf.resize(basePckr->workSize, basePckr->workSize / 2 + 1);
                Fctf = CtfHelper::getFftwImage(
                    ctf,
                    basePckr->workSize / 2 + 1, basePckr->workSize, basePckr->micrograph_size, basePckr->micrograph_size, basePckr->angpix,
                    &basePckr->obsModel,
                    false, false, basePckr->intact_ctf_first_peak, true
                );
                break;
            }
        }
    }
    }));
    #ifdef TIMING
    basePckr->timer.toc(basePckr->TIMING_A8);
    #endif

    #ifdef TIMING
    basePckr->timer.tic(basePckr->TIMING_A9);
    #endif
    CTICTOC(timer, "mccfResize", ({
    Mccf_best.resize(basePckr->workSize,basePckr->workSize);
    }));
    CTICTOC(timer, "mpsiResize", ({
    Mpsi_best.resize(basePckr->workSize,basePckr->workSize);
    }));
    #ifdef TIMING
    basePckr->timer.toc(basePckr->TIMING_A9);
    #endif
    AccPtr<acc::Complex> d_Fmic(allocator);
    AccPtr<XFLOAT> d_Mmean(allocator);
    AccPtr<XFLOAT> d_Mstddev(allocator);

    #ifdef TIMING
    basePckr->timer.tic(basePckr->TIMING_B1);
    #endif
    RFLOAT normfft = (RFLOAT) (basePckr->micrograph_size * basePckr->micrograph_size) / (RFLOAT) basePckr->nr_pixels_circular_mask;
    if (basePckr->do_read_fom_maps) {
        CTICTOC(timer, "readFromFomMaps_0", {
        FileName fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_stddevNoise.spi";
        Image<RFLOAT> It;
        It.read(fn_tmp);
        (basePckr->autopick_helical_segments ? Mstddev2 : Mstddev) = It();
        fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_avgNoise.spi";
        It.read(fn_tmp);
        (basePckr->autopick_helical_segments ? Mavg : Mmean) = It();
        });
    } else {
        /*
         * Squared difference FOM:
         * Sum ( (X-mu)/sig  - A )^2 =
         *  = Sum((X-mu)/sig)^2 - 2 Sum (A*(X-mu)/sig) + Sum(A)^2
         *  = (1/sig^2)*Sum(X^2) - (2*mu/sig^2)*Sum(X) + (mu^2/sig^2)*Sum(1) - (2/sig)*Sum(AX) + (2*mu/sig)*Sum(A) + Sum(A^2)
         *
         * However, the squared difference with an "empty" ie all-zero reference is:
         * Sum ( (X-mu)/sig)^2
         *
         * The ratio of the probabilities thereby becomes:
         * P(ref) = 1/sqrt(2pi) * exp (( (X-mu)/sig  - A )^2 / -2 )   // assuming sigma = 1!
         * P(zero) = 1/sqrt(2pi) * exp (( (X-mu)/sig )^2 / -2 )
         *
         * P(ref)/P(zero) = exp(( (X-mu)/sig  - A )^2 / -2) / exp ( ( (X-mu)/sig )^2 / -2)
         *                = exp( (- (2/sig)*Sum(AX) + (2*mu/sig)*Sum(A) + Sum(A^2)) / - 2 )
         *
         *                Therefore, I do not need to calculate (X-mu)/sig beforehand!!!
         *
         */

        CTICTOC(timer, "Imic_insert", ({
        std::copy_n(Imic.data.begin(), Imic.data.size(), micTransformer.reals.getHostPtr());
        micTransformer.reals.cpToDevice();
        }));

        CTICTOC(timer, "runCenterFFT_0", ({
        runCenterFFT(micTransformer.reals, micTransformer.sizer[0], micTransformer.sizer[1], true, 1);
        }));

        int FMultiBsize;
        CTICTOC(timer, "FourierTransform_0", ({
        micTransformer.forward();
        FMultiBsize = ((int) ceilf((float) micTransformer.fouriers.getSize() * 2 / (float) BLOCK_SIZE));
        CudaKernels::cuda_kernel_multi<XFLOAT><<<FMultiBsize, BLOCK_SIZE>>>(
            (XFLOAT*) micTransformer.fouriers.getAccPtr(),
            (XFLOAT) 1 / ((XFLOAT)(micTransformer.reals.getSize())),
            micTransformer.fouriers.getSize() * 2
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());
        }));

        if (basePckr->highpass > 0.0) {
            CTICTOC(timer, "highpass", ({
            micTransformer.fouriers.streamSync();
            lowPassFilterMapGPU(
                micTransformer.fouriers, (size_t) 1,
                micTransformer.sizef[1], micTransformer.sizef[0],
                Xsize(Imic()),
                basePckr->lowpass, basePckr->highpass, basePckr->angpix, 2, true
            ); //false = lowpass, true=highpass
            micTransformer.fouriers.streamSync();
            micTransformer.backward();
            micTransformer.reals.streamSync();
            }));
        }

        AccPtr<acc::Complex> Ftmp (allocator);
        CTICTOC(timer, "F_cp", ({
        Ftmp.setSize(micTransformer.fouriers.getSize());
        Ftmp.deviceAlloc();
        micTransformer.fouriers.cpOnAcc(Ftmp.getDevicePtr());
        }));

        // Also calculate the FFT of the squared micrograph
        CTICTOC(timer, "SquareImic", ({
        cuda_kernel_square<<<FMultiBsize, BLOCK_SIZE>>>(
            micTransformer.reals.getAccPtr(), micTransformer.reals.getSize()
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());
        }));

        CTICTOC(timer, "FourierTransform_1", ({
        micTransformer.forward();
        CudaKernels::cuda_kernel_multi<XFLOAT><<<FMultiBsize, BLOCK_SIZE>>>(
            (XFLOAT*) micTransformer.fouriers.getAccPtr(),
            (XFLOAT) 1 / ((XFLOAT) (micTransformer.reals.getSize())),
            micTransformer.fouriers.getSize() * 2
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());
        }));

        // The following calculate mu and sig under the solvent area at every position in the micrograph
        CTICTOC(timer, "calculateStddevAndMeanUnderMask", ({

        const size_t workSize2 = basePckr->workSize * basePckr->workSize;
        d_Mstddev.setSize(workSize2);
        d_Mstddev.deviceAlloc();
        d_Mmean.setSize(workSize2);
        d_Mmean.deviceAlloc();

        if (basePckr->autopick_helical_segments) {

            AccPtr<acc::Complex> d_Fmsk2(basePckr->Favgmsk.size(), allocator);
            AccPtr<XFLOAT> d_Mavg(allocator);
            AccPtr<XFLOAT> d_Mstddev2(allocator);

            d_Fmsk2.deviceAlloc();
            d_Mavg.setSize(workSize2);
            d_Mavg.deviceAlloc();
            d_Mstddev2.setSize(workSize2);
            d_Mstddev2.deviceAlloc();

            /// TODO: Do this only once further up in scope
            for (int i = 0; i < d_Fmsk2.getSize(); i++) {
                d_Fmsk2.getHostPtr()[i].x = basePckr->Favgmsk[i].real;
                d_Fmsk2.getHostPtr()[i].y = basePckr->Favgmsk[i].imag;
            }
            d_Fmsk2.cpToDevice();
            d_Fmsk2.streamSync();

            calculateStddevAndMeanUnderMask(
                Ftmp, micTransformer.fouriers, d_Fmsk2, basePckr->nr_pixels_avg_mask,
                d_Mstddev2, d_Mavg,
                micTransformer.sizef[0], micTransformer.sizef[1],
                basePckr->micrograph_size, basePckr->workSize
            );

            // Copy stddev
            d_Mstddev2.hostAlloc();
            d_Mstddev2.cpToHost();
            d_Mstddev2.streamSync();
            Mstddev2.resizeNoCp(basePckr->workSize, basePckr->workSize);
            std::copy_n(d_Mstddev2.getHostPtr(), d_Mstddev2.getSize(), Mstddev2.begin());

            // Copy avg
            d_Mavg.hostAlloc();
            d_Mavg.cpToHost();
            d_Mavg.streamSync();
            Mavg.resizeNoCp(basePckr->workSize, basePckr->workSize);
            std::copy_n(d_Mavg.getHostPtr(), d_Mavg.getSize(), Mavg.begin());
        }

        /// TODO: Do this only once further up in scope
        AccPtr<acc::Complex> d_Fmsk(basePckr->Finvmsk.size(), allocator);
        d_Fmsk.deviceAlloc();
        for (int i = 0; i < d_Fmsk.getSize() ; i++) {
            d_Fmsk.getHostPtr()[i].x = basePckr->Finvmsk[i].real;
            d_Fmsk.getHostPtr()[i].y = basePckr->Finvmsk[i].imag;
        }
        d_Fmsk.cpToDevice();
        d_Fmsk.streamSync();

        calculateStddevAndMeanUnderMask(
            Ftmp, micTransformer.fouriers, d_Fmsk,
            basePckr->nr_pixels_circular_invmask, d_Mstddev, d_Mmean,
            micTransformer.sizef[0], micTransformer.sizef[1],
            basePckr->micrograph_size, basePckr->workSize
        );


        /// TODO: remove this
        d_Mstddev.hostAlloc();
        d_Mstddev.cpToHost();
        d_Mstddev.streamSync();

        Mstddev.resizeNoCp(basePckr->workSize, basePckr->workSize);

        /// TODO: put this in a kernel
        for (int i = 0; i < d_Mstddev.getSize(); i++) {
            auto& x = d_Mstddev.getHostPtr()[i];
            Mstddev.data[i] = x;
            x = x > (XFLOAT) 1E-10 ? 1 / x : 1;
        }

        d_Mstddev.cpToDevice();
        d_Mstddev.streamSync();

        d_Mmean.hostAlloc();
        d_Mmean.cpToHost();
        d_Mmean.streamSync();
        Mmean.resizeNoCp(basePckr->workSize, basePckr->workSize);
        std::copy_n(d_Mmean.getHostPtr(), d_Mmean.getSize(), Mmean.begin());
        }));

        // From now on use downsized Fmic, as the cross-correlation with the references can be done at lower resolution
        CTICTOC(timer, "windowFourierTransform_0", ({
        d_Fmic.setSize((basePckr->workSize / 2 + 1) * basePckr->workSize);
        d_Fmic.deviceAlloc();
        windowFourierTransform2(
            Ftmp, d_Fmic,
            basePckr->micrograph_size / 2 + 1, basePckr->micrograph_size, 1, // Input dimensions
            basePckr->workSize / 2 + 1, basePckr->workSize, 1  // Output dimensions
        );
        }));

        if (basePckr->do_write_fom_maps) {
            CTICTOC(timer, "writeToFomMaps", ({
            // TMP output
            FileName fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_stddevNoise.spi";
            Image<RFLOAT>(basePckr->autopick_helical_segments ? Mstddev2 : Mstddev).write(fn_tmp);
            fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_avgNoise.spi";
            Image<RFLOAT>(basePckr->autopick_helical_segments ? Mavg : Mmean).write(fn_tmp);
            }));
        }
    }

    // Now start looking for the peaks of all references
    // Clear the output vector with all peaks
    std::vector<Peak> peaks;
    CTICTOC(timer, "initPeaks", ({
    peaks.clear();
    }));
    #ifdef TIMING
    basePckr->timer.toc(basePckr->TIMING_B1);
    #endif

    if (basePckr->autopick_helical_segments) {
        if (basePckr->do_read_fom_maps) {
            FileName fn_tmp;
            Image<RFLOAT> It_float;
            Image<int> It_int;

            fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedCCF.spi";
            It_float.read(fn_tmp);
            Mccf_best_combined = It_float();

            if (basePckr->do_amyloid) {
                fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedPSI.spi";
                It_float.read(fn_tmp);
                Mpsi_best_combined = It_float();
            } else {
                fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedCLASS.spi";
                It_int.read(fn_tmp);
                Mclass_best_combined = It_int();
            }
        } else {
            Mccf_best_combined.clear();
            Mccf_best_combined.resize(basePckr->workSize, basePckr->workSize);
            Mccf_best_combined = -99.e99;
            Mclass_best_combined.clear();
            Mclass_best_combined.resize(basePckr->workSize, basePckr->workSize);
            Mclass_best_combined = -1;
            Mpsi_best_combined.clear();
            Mpsi_best_combined.resize(basePckr->workSize, basePckr->workSize);
            Mpsi_best_combined = -99.e99;
        }
    }

    AccPtr<XFLOAT> d_ctf (Fctf.size(), allocator);
    d_ctf.deviceAlloc();
    if (basePckr->do_ctf) {
        std::copy_n(Fctf.begin(), Fctf.size(), d_ctf.getHostPtr());
        d_ctf.cpToDevice();
    }

    for (int iref = 0; iref < basePckr->Mrefs.size(); iref++) {

        CTICTOC(timer, "OneReference", ({

        RFLOAT expected_Pratio; // the expectedFOM for this (ctf-corrected) reference
        if (basePckr->do_read_fom_maps) {
            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B2);
            #endif
            if (!basePckr->autopick_helical_segments) {
                CTICTOC(timer, "readFromFomMaps", ({
                const FileName fn_mccf = FileName::compose(basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_ref", iref, "_bestCCF.spi");
                const auto image_best_ccf = Image<RFLOAT>::from_filename(fn_mccf);
                // Retrieve expected_Pratio from the image's header
                expected_Pratio = image_best_ccf.header.getValue<RFLOAT>(EMDL::IMAGE_STATS_MAX, image_best_ccf.header.size() - 1);
                Mccf_best = std::move(image_best_ccf.data);
                const FileName fn_mpsi = FileName::compose(basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_ref", iref, "_bestPSI.spi");
                const auto image_best_mpsi = Image<RFLOAT>::from_filename(fn_mpsi);
                Mpsi_best = std::move(image_best_mpsi.data);
                }));
            }
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B2);
            #endif
        } else {
            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B3);
            #endif
            CTICTOC(timer, "mccfInit", ({
            deviceInitValue(d_Mccf_best, (XFLOAT) -LARGE_NUMBER);
            }));
            AccProjectorKernel projKernel = AccProjectorKernel::makeKernel(
                projectors[iref],
                (int) basePckr->workSize / 2 + 1, (int) basePckr->workSize,
                1, // Zdim, always 1 in autopicker.
                (int) basePckr->workSize / 2 + 1 - 1
            );

            int FauxStride = (basePckr->workSize / 2 + 1) * basePckr->workSize;

            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B4);
            #endif
            CTICTOC(timer, "SingleProjection", ({
            dim3 blocks((int) ceilf((float) FauxStride / (float) BLOCK_SIZE), 1);
            if (basePckr->do_ctf) {
                cuda_kernel_rotateAndCtf<<<blocks, BLOCK_SIZE>>>(
                    cudaTransformer1.fouriers.getAccPtr(), d_ctf.getAccPtr(), 0, projKernel, 0
                );
            } else {
                cuda_kernel_rotateOnly<<<blocks, BLOCK_SIZE>>>(
                    cudaTransformer1.fouriers.getAccPtr(), 0, projKernel, 0
                );
            }
            LAUNCH_HANDLE_ERROR(cudaGetLastError());
            }));
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B4);
            #endif
            /*
             *    FIRST PSI WAS USED FOR PREP CALCS - THIS IS NOW A DEDICATED SECTION
             *    -------------------------------------------------------------------
             */

            CTICTOC(timer, "PREP_CALCS", ({

            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B5);
            #endif
            // Sjors 20 April 2016: The calculation for sum_ref_under_circ_mask, etc below needs to be done on original micrograph_size!
            CTICTOC(timer, "windowFourierTransform_FP", ({
            windowFourierTransform2(
                cudaTransformer1.fouriers, micTransformer.fouriers,
                basePckr->workSize / 2 + 1,        basePckr->workSize,        1, // Input dimensions
                basePckr->micrograph_size / 2 + 1, basePckr->micrograph_size, 1  // Output dimensions
            );
            }));

            CTICTOC(timer, "inverseFourierTransform_FP", ({
            micTransformer.backward();
            }));

            CTICTOC(timer, "runCenterFFT_FP", ({
            runCenterFFT(
                micTransformer.reals,
                (int) micTransformer.sizer[0], (int) micTransformer.sizer[1], false, 1
            );
            }));

            micTransformer.reals.cpToHost();

            Maux.resizeNoCp(basePckr->micrograph_size, basePckr->micrograph_size);

            micTransformer.reals.streamSync();
            std::copy_n(micTransformer.reals.getHostPtr(), micTransformer.reals.getSize(), Maux.begin());

            CTICTOC(timer, "setXmippOrigin_FP_0", ({
            Maux.setXmippOrigin();
            }));
            // TODO: check whether I need CenterFFT(Maux, false)
            // Sjors 20 April 2016: checked, somehow not needed.

            sum_ref_under_circ_mask = 0.0;
            sum_ref2_under_circ_mask = 0.0;
            RFLOAT suma2 = 0.0;
            RFLOAT sumn = 1.0;
            MultidimArray<RFLOAT> Mctfref(basePckr->particle_size, basePckr->particle_size);
            CTICTOC(timer, "setXmippOrigin_FP_1", ({
            Mctfref.setXmippOrigin();
            }));
            CTICTOC(timer, "suma_FP", ({
            // only loop over smaller Mctfref, but take values from large Maux!
            FOR_ALL_ELEMENTS_IN_ARRAY2D(Mctfref, i, j) {
                if (hypot2(i, j) < basePckr->particle_radius2) {
                    const auto& x = Maux.elem(i, j);
                    suma2 += x * x + 2.0 * x * rnd_gaus(0.0, 1.0);
                    sum_ref_under_circ_mask += x;
                    sum_ref2_under_circ_mask += x * x;
                    sumn += 1.0;
                }
            }
            sum_ref_under_circ_mask /= sumn;
            sum_ref2_under_circ_mask /= sumn;
            expected_Pratio = exp(suma2 / (2.0 * sumn));
            }));

            }));

            // for all batches
            CTICTOC(timer, "AllPsi", ({
            int startPsi(0);
            for (int psiIter = 0; psiIter < cudaTransformer1.batchIters; psiIter++) {
                // psi-batches for possible memory-limits

                CTICTOC(timer, "Projection", ({
                dim3 blocks((int) ceilf((float) FauxStride / (float) BLOCK_SIZE), cudaTransformer1.batchSize[psiIter]);
                if (basePckr->do_ctf) {
                    cuda_kernel_rotateAndCtf<<<blocks, BLOCK_SIZE>>>(
                        cudaTransformer1.fouriers.getAccPtr(),
                        d_ctf.getAccPtr(),
                        radians(basePckr->psi_sampling),
                        projKernel,
                        startPsi
                    );
                } else {
                    cuda_kernel_rotateOnly<<<blocks, BLOCK_SIZE>>>(
                        cudaTransformer1.fouriers.getAccPtr(),
                        radians(basePckr->psi_sampling),
                        projKernel,
                        startPsi
                    );
                }
                LAUNCH_HANDLE_ERROR(cudaGetLastError());
                }));
                // Now multiply (convolve) template and micrograph to calculate the cross-correlation
                CTICTOC(timer, "convol", ({
                dim3 blocks2((int) ceilf((float) FauxStride / (float) BLOCK_SIZE), cudaTransformer1.batchSize[psiIter]);
                cuda_kernel_batch_convol_A<<<blocks2, BLOCK_SIZE>>>(
                    cudaTransformer1.fouriers.getAccPtr(), d_Fmic.getAccPtr(), FauxStride
                );
                LAUNCH_HANDLE_ERROR(cudaGetLastError());
                }));

                CTICTOC(timer, "CudaInverseFourierTransform_1", ({
                cudaTransformer1.backward();
                HANDLE_ERROR(cudaDeviceSynchronize());
                }));

                CTICTOC(timer, "runCenterFFT_1", ({
                runCenterFFT(
                    cudaTransformer1.reals,
                    (int) cudaTransformer1.sizer[0], (int) cudaTransformer1.sizer[1],
                    false, cudaTransformer1.batchSize[psiIter]
                );
                }));
                // Calculate ratio of prabilities P(ref)/P(zero)
                // Keep track of the best values and their corresponding iref and psi
                // ------------------------------------------------------------------
                // So now we already had precalculated: Mdiff2 = 1/sig*Sum(X^2) - 2/sig*Sum(X) + mu^2/sig*Sum(1)
                // Still to do (per reference): - 2/sig*Sum(AX) + 2*mu/sig*Sum(A) + Sum(A^2)
                CTICTOC(timer, "probRatio", ({
                HANDLE_ERROR(cudaDeviceSynchronize());
                dim3 PR_blocks(ceilf((float) (cudaTransformer1.reals.getSize() / cudaTransformer1.batchSize[psiIter]) / (float) PROBRATIO_BLOCK_SIZE));
                cuda_kernel_probRatio<<<PR_blocks, PROBRATIO_BLOCK_SIZE>>>(
                    d_Mccf_best.getAccPtr(), d_Mpsi_best.getAccPtr(), cudaTransformer1.reals.getAccPtr(),
                    d_Mmean.getAccPtr(), d_Mstddev.getAccPtr(),
                    cudaTransformer1.reals.getSize() / cudaTransformer1.batchSize[0],
                    (XFLOAT) -2 * normfft,
                    (XFLOAT) 2 * sum_ref_under_circ_mask, (XFLOAT) sum_ref2_under_circ_mask,
                    (XFLOAT) expected_Pratio,
                    cudaTransformer1.batchSize[psiIter], startPsi, Npsi
                );
                LAUNCH_HANDLE_ERROR(cudaGetLastError());
                startPsi += cudaTransformer1.batchSize[psiIter];
                }));
            }
            }));
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B6);
            #endif
            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B7);
            #endif
            CTICTOC(timer, "output", ({
            d_Mccf_best.cpToHost();
            d_Mpsi_best.cpToHost();
            d_Mccf_best.streamSync();
            // Copy d_Mccf_best to Mccf_best
            // Copy d_Mpsi_best to Mpsi_best
            for (int i = 0; i < Mccf_best.size(); i++) {
                Mccf_best[i] = d_Mccf_best.getHostPtr()[i];
                Mpsi_best[i] = d_Mpsi_best.getHostPtr()[i];
            }
            }));

            if (basePckr->do_write_fom_maps && !basePckr->autopick_helical_segments) {
                CTICTOC(timer, "writeFomMaps", ({
                Image<RFLOAT> IMccf_best (Mccf_best);
                // Store expected_Pratio in the header of the image..
                IMccf_best.header.setValue(EMDL::IMAGE_STATS_MAX, expected_Pratio, IMccf_best.header.size() - 1);  // Store expected_Pratio in the header of the image
                const auto fn_Mccf_best = FileName::compose(basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_ref", iref, "_bestCCF.spi");
                IMccf_best.write(fn_Mccf_best);

                Image<RFLOAT> IMpsi_best (Mpsi_best);
                const auto fn_Mpsi_best = FileName::compose(basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_ref", iref, "_bestPSI.spi");
                IMpsi_best.write(fn_Mpsi_best);
                }));
            }
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B7);
            #endif
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B3);
            #endif
        }

        /// TODO: FIX HELICAL SEGMENTS SUPPORT
        if (basePckr->autopick_helical_segments) {
            if (!basePckr->do_read_fom_maps) {
                // Combine Mccf_best and Mpsi_best from all refs
                for (long int n = 0; n < Mccf_best.size(); n++) {
                    RFLOAT new_ccf = Mccf_best[n];
                    RFLOAT old_ccf = Mccf_best_combined[n];
                    if (new_ccf > old_ccf) {
                        Mccf_best_combined[n] = new_ccf;
                        if (basePckr->do_amyloid) {
                            Mpsi_best_combined[n] = Mpsi_best[n];
                        } else {
                            Mclass_best_combined[n] = iref;
                        }
                    }
                }
            }
        } else {
            #ifdef TIMING
            basePckr->timer.tic(basePckr->TIMING_B8);
            #endif
            // Now that we have Mccf_best and Mpsi_best, get the peaks
            std::vector<Peak> my_ref_peaks;
            CTICTOC(timer, "setXmippOriginX3", ({
            Mstddev.setXmippOrigin();
            Mmean.setXmippOrigin();
            Mccf_best.setXmippOrigin();
            Mpsi_best.setXmippOrigin();
            }));

            CTICTOC(timer, "peakSearch", ({
            basePckr->peakSearch(Mccf_best, Mpsi_best, Mstddev, Mmean, iref, my_skip_side, my_ref_peaks, scale);
            }));

            CTICTOC(timer, "peakPrune", ({
            basePckr->prunePeakClusters(my_ref_peaks, min_distance_pix, scale);
            }));

            CTICTOC(timer, "peakInsert", ({
            // append the peaks of this reference to all the other peaks
            peaks.insert(peaks.end(), my_ref_peaks.begin(), my_ref_peaks.end());
            }));
            #ifdef TIMING
            basePckr->timer.toc(basePckr->TIMING_B8);
            #endif
        }

        }));
    }

    if (basePckr->autopick_helical_segments) {
        if (basePckr->do_write_fom_maps) {
            FileName fn_tmp;
            Image<RFLOAT> It_float;
            Image<int> It_int;

            It_float() = Mccf_best_combined;
            fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedCCF.spi";
            It_float.write(fn_tmp);

            if (basePckr->do_amyloid) {
                It_float() = Mpsi_best_combined;
                fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedPSI.spi";
                It_float.write(fn_tmp);
            } else {
                It_int() = Mclass_best_combined;
                fn_tmp = basePckr->getOutputRootName(fn_mic) + + "_" + basePckr->fn_out + "_combinedCLASS.spi";
                It_int.write(fn_tmp);
            }
        } // end if do_write_fom_maps

        RFLOAT thres = basePckr->min_fraction_expected_Pratio;
        int peak_r_min = 1;
        std::vector<ccfPeak> ccf_peak_list;
        std::vector<std::vector<ccfPeak> > tube_coord_list, tube_track_list;
        std::vector<RFLOAT> tube_len_list;
        MultidimArray<RFLOAT> Mccfplot;

        Mccf_best_combined.setXmippOrigin();
        Mpsi_best_combined.setXmippOrigin();
        Mstddev2.setXmippOrigin();
        Mavg.setXmippOrigin();
        Mclass_best_combined.setXmippOrigin();
        if (basePckr->do_amyloid) {
            basePckr->pickAmyloids(
                Mccf_best_combined, Mpsi_best_combined, Mstddev2, Mavg, thres,
                basePckr->amyloid_max_psidiff, fn_mic, basePckr->fn_out,
                basePckr->helical_tube_diameter / basePckr->angpix,
                basePckr->autopick_skip_side, scale
            );
        } else {
            basePckr->pickCCFPeaks(
                Mccf_best_combined, Mstddev2, Mavg, Mclass_best_combined, thres,
                peak_r_min,
                basePckr->particle_diameter / basePckr->angpix,
                ccf_peak_list, Mccfplot, my_skip_side, scale
            );
            basePckr->extractHelicalTubes(
                ccf_peak_list, tube_coord_list, tube_len_list, tube_track_list,
                basePckr->particle_diameter / basePckr->angpix,
                basePckr->helical_tube_curvature_factor_max,
                basePckr->min_particle_distance / basePckr->angpix,
                basePckr->helical_tube_diameter / basePckr->angpix, scale
            );
            basePckr->exportHelicalTubes(
                Mccf_best_combined, Mccfplot, Mclass_best_combined,
                tube_coord_list, tube_track_list, tube_len_list,
                fn_mic, basePckr->fn_out,
                basePckr->particle_diameter / basePckr->angpix,
                basePckr->helical_tube_length_min / basePckr->angpix,
                my_skip_side, scale
            );
        }


        if ((
            basePckr->do_write_fom_maps || basePckr->do_read_fom_maps
        ) && !basePckr->do_amyloid) {
            Image<RFLOAT> It;
            It() = Mccfplot;
            // It.data = Mccfplot
            FileName fn_tmp = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + "_combinedPLOT.spi";
            It.write(fn_tmp);
        }
    } else {
        #ifdef TIMING
        basePckr->timer.tic(basePckr->TIMING_B9);
        #endif
        //Now that we have done all references, prune the list again...
        CTICTOC(timer, "finalPeakPrune", {
        basePckr->prunePeakClusters(peaks, min_distance_pix, scale);
        });

        // And remove all too close neighbours
        basePckr->removeTooCloselyNeighbouringPeaks(peaks, min_distance_pix, scale);

        // Write out a STAR file with the coordinates
        MetaDataTable MDout;
        for (const auto &peak : peaks) {
            const long int i = MDout.addObject();
            MDout.setValue(EMDL::IMAGE_COORD_X, (RFLOAT) (peak.x) / scale, i);
            MDout.setValue(EMDL::IMAGE_COORD_Y, (RFLOAT) (peak.y) / scale, i);
            MDout.setValue(EMDL::PARTICLE_CLASS, peak.ref + 1, i); // start counting at 1
            MDout.setValue(EMDL::PARTICLE_AUTOPICK_FOM, peak.fom, i);
            MDout.setValue(EMDL::ORIENT_PSI, peak.psi, i);
        }
        const FileName fn = basePckr->getOutputRootName(fn_mic) + "_" + basePckr->fn_out + ".star";
        MDout.write(fn);
        #ifdef TIMING
        basePckr->timer.toc(basePckr->TIMING_B9);
        #endif
    }
}
