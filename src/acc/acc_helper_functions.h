#ifndef ACC_HELPER_FUNCTIONS_H_
#define ACC_HELPER_FUNCTIONS_H_

#include "src/acc/acc_ml_optimiser.h"

/*
 * This assisting function goes over the orientations determined as significant for this image, and checks
 * which translations should be included in the list of those which differences will be calculated for.
 *
 * Any contiguous translations with a shared orientation are grouped together into a "job" which is supplied
 * to the difference kernel. If there are more contiguous translations than the specified "chunk" number,
 * these are split into separate jobs, to increase parallelism at the cost of redundant memory reads.
 */
long int makeJobsForDiff2Fine(
        OptimisationParamters &op,  SamplingParameters &sp,
        long int orientation_num, long int translation_num,
        ProjectionParams &FineProjectionData,
        std::vector< long unsigned > &iover_transes,
        std::vector< long unsigned > &ihiddens,
        long int nr_over_orient, long int nr_over_trans, int img_id,
        IndexedDataArray &FPW, // FPW=FinePassWeights
        IndexedDataArrayMask &dataMask,
        int chunk);

/*
 * This assisting function goes over the weight-array and groups all weights with shared
 * orientations into 'jobs' which are fed into the collect-kenrel, which reduces all translations
 * with computed differences into a reduced object to be back-projected.
 */
long int  makeJobsForCollect(
    IndexedDataArray &FPW,  // FPW = FinePassWeights
    IndexedDataArrayMask &dataMask,
    unsigned long NewJobNum
);

/*
 * Maps weights to a decoupled indexing of translations and orientations
 */
void mapWeights(
    unsigned long orientation_start,
    XFLOAT *mapped_weights,
    unsigned long orientation_num,
    unsigned long idxArr_start, unsigned long idxArr_end,
    unsigned long translation_num,
    XFLOAT *weights,
    long unsigned *rot_idx, long unsigned *trans_idx,
    unsigned long current_oversampling
);

void buildCorrImage(
    MlOptimiser *baseMLO,
    OptimisationParamters &op,
    AccPtr<XFLOAT> &corr_img,
    int img_id, long int group_id,
    bool ctf_premultiplied
);

void generateEulerMatrices(
    ProjectionParams &ProjectionData,
    XFLOAT *eulers,
    bool inverse,
    Matrix<RFLOAT> &L, Matrix<RFLOAT> &R
);

long unsigned generateProjectionSetupFine(
    OptimisationParamters &op,
    SamplingParameters &sp,
    MlOptimiser *baseMLO,
    unsigned iclass,
    ProjectionParams &ProjectionData
);

void runWavgKernel(
    AccProjectorKernel &projector,
    XFLOAT *eulers,
    XFLOAT *Fimgs_real, XFLOAT *Fimgs_imag,
    XFLOAT *trans_x, XFLOAT *trans_y, XFLOAT *trans_z,
    XFLOAT *sorted_weights,
    XFLOAT *ctfs,
    XFLOAT *wdiff2s_parts, XFLOAT *wdiff2s_AA, XFLOAT *wdiff2s_XA,
    OptimisationParamters &op,
    long unsigned orientation_num, long unsigned translation_num,
    unsigned long image_size,
    int img_id, int group_id, int exp_iclass,
    XFLOAT part_scale,
    bool refs_are_ctf_corrected, bool ctf_premultiplied, bool data_is_3D,
    cudaStream_t stream
);

void runBackProjectKernel(
    AccBackprojector &BP,
    AccProjectorKernel &projector,
    XFLOAT *d_img_real, XFLOAT *d_img_imag,
    XFLOAT *trans_x, XFLOAT *trans_y, XFLOAT *trans_z,
    XFLOAT* d_weights,
    XFLOAT* d_Minvsigma2s,
    XFLOAT* d_ctfs,
    unsigned long translation_num,
    XFLOAT significant_weight,
    XFLOAT weight_norm,
    XFLOAT *d_eulers,
    int imgX, int imgY, int imgZ,
    unsigned long imageCount,
    bool data_is_3D, bool do_sgd, bool ctf_premultiplied,
    cudaStream_t optStream
);

template <typename T>
void deviceInitComplexValue(AccPtr<T> &data, XFLOAT value) {
    AccUtilities::InitComplexValue<T>(data, value);
}

template <typename T>
void deviceInitValue(AccPtr<T> &data, T value) {
    AccUtilities::InitValue<T>(data, value);
}

template <typename T>
void deviceInitValue(AccPtr<T> &data, T value, size_t Size) {
    AccUtilities::InitValue<T>(data, value, Size);
}

void mapAllWeightsToMweights(
    unsigned long *d_iorient,  //projectorPlan.iorientclasses
    XFLOAT * d_allweights,  // allWeights
    XFLOAT * d_mweights,  // Mweight
    unsigned long orientation_num,  // projectorPlan.orientation_num
    unsigned long translation_num,  // translation_num
    cudaStream_t stream
);

#define OVER_THRESHOLD_BLOCK_SIZE 512
template< typename T>
void arrayOverThreshold(AccPtr<T> &data, AccPtr<bool> &passed, T threshold) {
    #ifdef CUDA
    int grid_size = ceil((float) data.getSize() / (float) OVER_THRESHOLD_BLOCK_SIZE);
    cuda_kernel_array_over_threshold<T><<<grid_size, OVER_THRESHOLD_BLOCK_SIZE, 0, data.getStream()>>>(
        data.getAccPtr(),
        passed.getAccPtr(),
        threshold,
        data.getSize(),
        OVER_THRESHOLD_BLOCK_SIZE
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
    #else
    for (size_t i = 0; i < data.getSize(); i++) {
        passed.getHostPtr()[i] = data.getHostPtr()[i] >= threshold;
    }
    #endif
}

#define FIND_IN_CUMULATIVE_BLOCK_SIZE 512
template<typename T>
size_t findThresholdIdxInCumulativeSum(AccPtr<T> &data, T threshold) {
    int grid_size = ceil((float)(data.getSize()-1)/(float)FIND_IN_CUMULATIVE_BLOCK_SIZE);
    if (grid_size == 0) {
        return(0);
    } else {
        #ifdef CUDA
        AccPtr<size_t> idx (1, data.getAllocator(), data.getStream());
        idx.getHostPtr()[0] = 0;

        idx.putOnDevice();
        cuda_kernel_find_threshold_idx_in_cumulative<<<grid_size, FIND_IN_CUMULATIVE_BLOCK_SIZE, 0, data.getStream()>>>(
            data.getAccPtr(),
            threshold,
            data.getSize() - 1,
            idx.getAccPtr(),
            FIND_IN_CUMULATIVE_BLOCK_SIZE
        );
        idx.cpToHost();
        DEBUG_HANDLE_ERROR(cudaStreamSynchronize(data.getStream()));
        return idx.getHostPtr()[0];
        #else
        size_t idx = 0;
        size_t size_m1 = data.getSize()-1;
        for (size_t i = 0; i < size_m1; i++) {
            if (data.getHostPtr()[i] <= threshold && threshold < data.getHostPtr()[i + 1])
                idx = i + 1;
        }
        return idx;
        #endif
    }
}

void runDiff2KernelCoarse(
    AccProjectorKernel &projector,
    XFLOAT *trans_x, XFLOAT *trans_y, XFLOAT *trans_z,
    XFLOAT *corr_img,
    XFLOAT *Fimg_real, XFLOAT *Fimg_imag,
    XFLOAT *d_eulers,
    XFLOAT *diff2s,
    XFLOAT local_sqrtXi2,
    long unsigned orientation_num,
    unsigned long translation_num, unsigned long image_size,
    cudaStream_t stream,
    bool do_CC, bool data_is_3D
);

void runDiff2KernelFine(
    AccProjectorKernel &projector,
    XFLOAT *corr_img,
    XFLOAT *Fimgs_real, XFLOAT *Fimgs_imag,
    XFLOAT *trans_x, XFLOAT *trans_y, XFLOAT *trans_z,
    XFLOAT *eulers,
    long unsigned *rot_id, long unsigned *rot_idx,
    long unsigned *trans_idx,
    long unsigned *job_idx, long unsigned *job_num,
    XFLOAT *diff2s,
    OptimisationParamters &op,
    MlOptimiser *baseMLO,
    long unsigned orientation_num, long unsigned translation_num, long unsigned significant_num,
    unsigned long image_size,
    int img_id,
    int exp_iclass,
    cudaStream_t stream,
    long unsigned job_num_count,
    bool do_CC, bool data_is_3D
);

void runCollect2jobs(
    int grid_dim,
    XFLOAT *oo_otrans_x,          // otrans-size -> make const
    XFLOAT *oo_otrans_y,          // otrans-size -> make const
    XFLOAT *oo_otrans_z,          // otrans-size -> make const
    XFLOAT *myp_oo_otrans_x2y2z2, // otrans-size -> make const
    XFLOAT *weights,
    XFLOAT significant_weight,    // TODO Put in const
    XFLOAT sum_weight,    		  // TODO Put in const
    unsigned long nr_trans,
    unsigned long oversampled_trans, unsigned long oversampled_rot,
    unsigned long oversamples,
    bool skip_rots,
    XFLOAT *p_weights,
    XFLOAT *p_thr_wsum_prior_offsetx_class, XFLOAT *p_thr_wsum_prior_offsety_class, XFLOAT *p_thr_wsum_prior_offsetz_class,
    XFLOAT *p_thr_wsum_sigma2_offset,
    size_t *rot_idx, size_t *trans_idx,
    size_t *jobOrigin, size_t *jobExtent,
    bool data_is_3D
);

void windowFourierTransform2(
    AccPtr<acc::Complex> &d_in,
    AccPtr<acc::Complex> &d_out,
    size_t iX, size_t iY, size_t iZ,  // Input dimensions
    size_t oX, size_t oY, size_t oZ,  // Output dimensions
    size_t Npsi = 1,
    size_t pos = 0,
    cudaStream_t stream = 0
);

void selfApplyBeamTilt2(
    MultidimArray<Complex> &Fimg, RFLOAT beamtilt_x, RFLOAT beamtilt_y,
    RFLOAT wavelength, RFLOAT Cs, RFLOAT angpix, int ori_size
);

template <typename T>
void runCenterFFT(MultidimArray<T>& v, bool forward, CudaCustomAllocator *allocator) {
    #ifdef CUDA
    using AccUtilities::GpuKernels::centerFFT_3D;
    #else
    using AccUtilities::CpuKernels::centerFFT_3D;
    #endif
    AccPtr<XFLOAT> img_in (v.size(), allocator);   // with original data pointer
	// AccPtr<XFLOAT>  img_aux (v.size(), allocator);   // temporary holder

    std::copy_n(v.begin(), v.size(), img_in.getHostPtr());

    img_in.putOnDevice();
	// img_aux.deviceAlloc();

    if (v.getDim() == 1) {
        std::cerr << "CenterFFT on gpu reverts to cpu for dim!=2 (now dim=1)" <<std::endl;
        // 1D

        int l = v.size();
        int shift = l / 2;
        MultidimArray<T> aux (l);

        if (!forward) { shift = -shift; }

        // Shift the input in an auxiliary vector
        for (int i = 0; i < l; i++) {
            int ip = i + shift;

            if (ip < 0) {
                ip += l;
            } else if (ip >= l) {
                ip -= l;
            }

            aux[ip] = v[i];
        }

        v = std::move(aux);

    } else if (v.getDim() == 2) {
        // 2D
        // std::cerr << "CenterFFT on gpu with dim=2!" <<std::endl;

        long int xshift = Xsize(v) / 2;
        long int yshift = Ysize(v) / 2;

        if (!forward) {
            xshift = -xshift;
            yshift = -yshift;
        }

        int dim = ceilf((float) (v.size() / (float) (2 * CFTT_BLOCK_SIZE)));
        centerFFT_2D(
            dim, 0, CFTT_BLOCK_SIZE,
            #ifdef CUDA
            img_in.getAccPtr(),
            #else
            img_in.getHostPtr(),
            #endif
            v.size(),
            Xsize(v), Ysize(v),
            xshift, yshift
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());

        img_in.cpToHost();

        std::copy_n(img_in.getHostPtr(), v.size(), v.begin());

    } else if (v.getDim() == 3) {
        // TODO - convert this to use the faster AccUtilities::centerFFT_3D like the 2D case above
        // and in fftw.h when FAST_CENTERFFT is defined
        std::cerr << "CenterFFT on gpu reverts to cpu for dim!=2 (now dim=3)" << std::endl;
        // 3D

        // Shift in the X direction
        int l = Xsize(v);
        MultidimArray<T> aux (l);
        int shift = l / 2;

        if (!forward) { shift = -shift; }

        for (int k = 0; k < Zsize(v); k++)
        for (int j = 0; j < Ysize(v); j++) {
            // Shift the input in an auxiliary vector
            for (int i = 0; i < l; i++) {
                int ip = i + shift;

                if (ip < 0) {
                    ip += l;
                } else if (ip >= l) {
                    ip -= l;
                }

                aux(ip) = direct::elem(v, i, j, k);
            }

            // Copy the vector
            for (int i = 0; i < l; i++)
                direct::elem(v, i, j, k) = direct::elem(aux, i);
        }

        // Shift in the Y direction
        l = Ysize(v);
        aux.resize(l);
        shift = l / 2;

        if (!forward) { shift = -shift; }

        for (int k = 0; k < Zsize(v); k++)
        for (int i = 0; i < Xsize(v); i++) {
            // Shift the input in an auxiliary vector
            for (int j = 0; j < l; j++) {
                int jp = j + shift;

                if (jp < 0) {
                    jp += l;
                } else if (jp >= l) {
                    jp -= l;
                }

                aux(jp) = direct::elem(v, i, j, k);
            }

            // Copy the vector
            for (int j = 0; j < l; j++)
                direct::elem(v, i, j, k) = direct::elem(aux, j);
        }

        // Shift in the Z direction
        l = Zsize(v);
        aux.resize(l);
        shift = l / 2;

        if (!forward) { shift = -shift; }

        for (int j = 0; j < Ysize(v); j++)
        for (int i = 0; i < Xsize(v); i++) {
            // Shift the input in an auxiliary vector
            for (int k = 0; k < l; k++) {
                int kp = k + shift;
                if (kp < 0) {
                    kp += l;
                } else if (kp >= l) {
                    kp -= l;
                }

                aux(kp) = direct::elem(v, i, j, k);
            }

            // Copy the vector
            for (int k = 0; k < l; k++)
                direct::elem(v, i, j, k) = direct::elem(aux, k);
        }
    } else {
        v.printShape();
        REPORT_ERROR("CenterFFT ERROR: Dimension should be 1, 2 or 3");
    }
}


template <typename T>
void runCenterFFT(
    AccPtr<T> &img_in, int xSize, int ySize, bool forward, int batchSize = 1
) {
	// AccPtr<XFLOAT> img_aux (img_in.h_ptr, img_in.getSize(), allocator);   // temporary holder
	// img_aux.deviceAlloc();
    #ifdef CUDA
    using AccUtilities::GpuKernels::centerFFT_2D;
    #else
    using AccUtilities::CpuKernels::centerFFT_2D;
    #endif

    int xshift = xSize / 2;
    int yshift = ySize / 2;

    if (!forward) {
        xshift = -xshift;
        yshift = -yshift;
    }

    const int blocks = ceilf((float) ((xSize * ySize) / (float) (2 * CFTT_BLOCK_SIZE)));
    centerFFT_2D(
        blocks, batchSize, CFTT_BLOCK_SIZE,
        img_in.getStream(),
        img_in.getAccPtr(),
        xSize * ySize,
        xSize, ySize,
        xshift, yshift    
    );

    LAUNCH_HANDLE_ERROR(cudaGetLastError());

//	HANDLE_ERROR(cudaStreamSynchronize(0));
//	img_aux.cpOnDevice(img_in.d_ptr); //update input image with centered kernel-output.
}

template <typename T>
void runCenterFFT(
    AccPtr<T> &img_in, int xSize, int ySize, int zSize, bool forward, int batchSize = 1
) {
	// AccPtr<XFLOAT> img_aux (img_in.h_ptr, img_in.getSize(), allocator);   // temporary holder
	// img_aux.deviceAlloc();
    #ifdef CUDA
    using AccUtilities::GpuKernels::centerFFT_2D;
    using AccUtilities::GpuKernels::centerFFT_3D;
    #else
    using AccUtilities::CpuKernels::centerFFT_2D;
    using AccUtilities::CpuKernels::centerFFT_3D;
    #endif

    if (zSize > 1) {
        int xshift = xSize / 2;
        int yshift = ySize / 2;
        int zshift = ySize / 2;

        if (!forward) {
            xshift = -xshift;
            yshift = -yshift;
            zshift = -zshift;
        }

        int grid_size = ceilf((float) (((size_t) xSize * (size_t) ySize * (size_t) zSize) /
            (float) (2 * CFTT_BLOCK_SIZE)));
        centerFFT_3D(
            grid_size, batchSize, CFTT_BLOCK_SIZE,
            img_in.getStream(),
            img_in.getAccPtr(),
            (size_t) xSize * (size_t) ySize * (size_t) zSize,
            xSize, ySize, zSize,
            xshift, yshift, zshift
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());
        //	HANDLE_ERROR(cudaStreamSynchronize(0));
        //	img_aux.cpOnDevice(img_in.d_ptr); //update input image with centered kernel-output.
    } else {
        int xshift = xSize / 2;
        int yshift = ySize / 2;

        if (!forward) {
            xshift = -xshift;
            yshift = -yshift;
        }

        int blocks = ceilf((float) ((xSize * ySize) / (float) (2 * CFTT_BLOCK_SIZE)));
        centerFFT_2D(
            blocks, batchSize, CFTT_BLOCK_SIZE,
            img_in.getStream(),
            img_in.getAccPtr(),
            xSize * ySize,
            xSize, ySize,
            xshift, yshift
        );
        LAUNCH_HANDLE_ERROR(cudaGetLastError());
    }
}

template <typename T>
void lowPassFilterMapGPU(
    AccPtr<T> &img_in,
    size_t Zdim, size_t Ydim, size_t Xdim,
    long int ori_size,
    RFLOAT lowpass, RFLOAT highpass,
    RFLOAT angpix,
    int filter_edge_width,
    bool do_highpass
) {
    // High or low?
    RFLOAT passLimit = do_highpass ? highpass : lowpass;

    // Which resolution shell is the filter?
    int ires_filter = round(ori_size * angpix / passLimit);
    int filter_edge_halfwidth = filter_edge_width / 2;

    // Soft-edge: from 1 shell less to one shell more:
    XFLOAT edge_low  = std::max(0.0,           (ires_filter - filter_edge_halfwidth) / (RFLOAT) ori_size); // in 1/pix
    XFLOAT edge_high = std::min((double) Xdim, (ires_filter + filter_edge_halfwidth) / (RFLOAT) ori_size); // in 1/pix
    XFLOAT edge_width = edge_high - edge_low;

    const int blocks = ceilf(
        (float) ((size_t) Xdim * (size_t) Ydim * (size_t) Zdim) /
        (float) CFTT_BLOCK_SIZE
    );

    AccUtilities::frequencyPass<acc::type>(
        blocks, CFTT_BLOCK_SIZE, img_in.getStream(),
        img_in.getAccPtr(), ori_size, Xdim, Ydim, Zdim,
        edge_low, edge_width, edge_high, (XFLOAT) angpix,
        (size_t) Xdim * (size_t) Ydim * (size_t) Zdim, do_highpass
    );
    LAUNCH_HANDLE_ERROR(cudaGetLastError());
}

#endif  // ACC_HELPER_FUNCTIONS_H_
