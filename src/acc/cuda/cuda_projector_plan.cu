#include "src/acc/cuda/cuda_projector_plan.h"
#include "src/time.h"
#include <cuda_runtime.h>

#ifdef CUDA
//#include <cuda_runtime.h>
#ifdef CUDA_FORCESTL
#include "src/acc/cuda/cuda_utils_stl.cuh"
#else
#include "src/acc/cuda/cuda_utils_cub.cuh"
#endif
#endif

#include "src/acc/utilities.h"
#include "src/acc/acc_projector_plan_impl.h"  // getOrientations

// #define PP_TIMING
#ifdef PP_TIMING
Timer timer;
int TIMING_TOP = timer.setNew("setup");
int TIMING_SAMPLING = 	timer.setNew(" sampling");
int TIMING_PRIOR = 		timer.setNew("  prior");
int TIMING_PROC_CALC = 	timer.setNew("  procCalc");
int TIMING_PROC = 		timer.setNew("  proc");
int TIMING_GEN = 		timer.setNew("   genOri");
int TIMING_PERTURB = 	timer.setNew("   perturb");
int TIMING_EULERS = 	timer.setNew(" eulers");
#define TIMING_TIC(id) timer.tic(id)
#define TIMING_TOC(id) timer.toc(id)
#else
#define TIMING_TIC(id)
#define TIMING_TOC(id)
#endif

/// HACK: Imitate a context manager.
#define TICTOC(n, block) TIMING_TIC(n); block; TIMING_TOC(n);

template<bool invert>
__global__ void cuda_kernel_make_eulers_2D(
    XFLOAT *alphas, XFLOAT *eulers, unsigned orientation_num
) {
    unsigned oid = blockIdx.x * BLOCK_SIZE + threadIdx.x; //Orientation id

    if (oid >= orientation_num) return;

    XFLOAT ca, sa;
    XFLOAT a = alphas[oid] * (XFLOAT)PI / (XFLOAT)180.0;

    #ifdef CUDA_DOUBLE_PRECISION
        sincos(a, &sa, &ca);
    #else
        sincosf(a, &sa, &ca);
    #endif

    if (!invert) {
        eulers[9 * oid + 0] =  ca;  // 00
        eulers[9 * oid + 1] =  sa;  // 01
        eulers[9 * oid + 2] =   0;  // 02
        eulers[9 * oid + 3] = -sa;  // 10
        eulers[9 * oid + 4] =  ca;  // 11
        eulers[9 * oid + 5] =   0;  // 12
        eulers[9 * oid + 6] =   0;  // 20
        eulers[9 * oid + 7] =   0;  // 21
        eulers[9 * oid + 8] =   1;  // 22
    } else {
        eulers[9 * oid + 0] =  ca;  // 00
        eulers[9 * oid + 1] = -sa;  // 10
        eulers[9 * oid + 2] =   0;  // 20
        eulers[9 * oid + 3] =  sa;  // 01
        eulers[9 * oid + 4] =  ca;  // 11
        eulers[9 * oid + 5] =   0;  // 21
        eulers[9 * oid + 6] =   0;  // 02
        eulers[9 * oid + 7] =   0;  // 12
        eulers[9 * oid + 8] =   1;  // 22
    }
}

template<bool invert,bool perturb>
__global__ void cuda_kernel_make_eulers_3D(
    XFLOAT *alphas, XFLOAT *betas, XFLOAT *gammas, XFLOAT *eulers,
    unsigned orientation_num,
    XFLOAT *R
) {
    XFLOAT a(0.f),b(0.f),g(0.f), A[9],B[9];
    XFLOAT ca, sa, cb, sb, cg, sg, cc, cs, sc, ss;

    unsigned oid = blockIdx.x * BLOCK_SIZE + threadIdx.x; //Orientation id

    if (oid >= orientation_num)
        return;

    for (int i = 0; i < 9; i ++)
        B[i] = (XFLOAT) 0.f;

    a = alphas[oid] * (XFLOAT) PI / (XFLOAT) 180.0;
    b = betas[oid]  * (XFLOAT) PI / (XFLOAT) 180.0;
    g = gammas[oid] * (XFLOAT) PI / (XFLOAT) 180.0;

    #ifdef CUDA_DOUBLE_PRECISION
        sincos(a, &sa, &ca);
        sincos(b,  &sb, &cb);
        sincos(g, &sg, &cg);
    #else
        sincosf(a, &sa, &ca);
        sincosf(b,  &sb, &cb);
        sincosf(g, &sg, &cg);
    #endif

    cc = cb * ca;
    cs = cb * sa;
    sc = sb * ca;
    ss = sb * sa;

    A[0] = ( cg * cc - sg * sa);  // 00
    A[1] = ( cg * cs + sg * ca);  // 01
    A[2] = (-cg * sb )         ;  // 02
    A[3] = (-sg * cc - cg * sa);  // 10
    A[4] = (-sg * cs + cg * ca);  // 11
    A[5] = ( sg * sb )         ;  // 12
    A[6] = ( sc )              ;  // 20
    A[7] = ( ss )              ;  // 21
    A[8] = ( cb )              ;  // 22


    if (perturb)
        for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++) {
            B[i * 3 + j] += A[i * 3 + k] * R[k * 3 + j];
        }
    else
        for (int i = 0; i < 9; i++)
            B[i] = A[i];

    if (invert) {
        for (int i = 0; i < 3; i++) 
        for (int j = 0; j < 3; j++) {
            eulers[9 * oid + i] = B[3 * j + i];
        }
        /*
        eulers[9 * oid + 0] = B[0];  // 00
        eulers[9 * oid + 1] = B[3];  // 01
        eulers[9 * oid + 2] = B[6];  // 02
        eulers[9 * oid + 3] = B[1];  // 10
        eulers[9 * oid + 4] = B[4];  // 11
        eulers[9 * oid + 5] = B[7];  // 12
        eulers[9 * oid + 6] = B[2];  // 20
        eulers[9 * oid + 7] = B[5];  // 21
        eulers[9 * oid + 8] = B[8];  // 22
        */
    } else {
        for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            eulers[9 * oid + i] = B[3 * i + j];
        }
        /*
        eulers[9 * oid + 0] = B[0];  // 00
        eulers[9 * oid + 1] = B[1];  // 10
        eulers[9 * oid + 2] = B[2];  // 20
        eulers[9 * oid + 3] = B[3];  // 01
        eulers[9 * oid + 4] = B[4];  // 11
        eulers[9 * oid + 5] = B[5];  // 21
        eulers[9 * oid + 6] = B[6];  // 02
        eulers[9 * oid + 7] = B[7];  // 12
        eulers[9 * oid + 8] = B[8];  // 22
        */
    }
}

void CudaProjectorPlan::setup(
    HealpixSampling &sampling,
    std::vector<RFLOAT> &directions_prior, std::vector<RFLOAT> &psi_prior,
    std::vector<int> &pointer_dir_nonzeroprior, std::vector<int> &pointer_psi_nonzeroprior,
    MultidimArray<bool> *Mcoarse_significant,
    std::vector<RFLOAT> &pdf_class,
    std::vector<MultidimArray<RFLOAT>> &pdf_direction,
    unsigned long nr_dir, unsigned long nr_psi,
    unsigned long idir_min, unsigned long idir_max,
    unsigned long ipsi_min, unsigned long ipsi_max,
    unsigned long itrans_min, unsigned long itrans_max,
    unsigned long current_oversampling, unsigned long nr_oversampled_rot,
    unsigned iclass,
    bool coarse, bool inverseMatrix, bool do_skip_align, bool do_skip_rotate,
    int orientational_prior_mode
) {
    TICTOC(TIMING_TOP, ({

    std::vector<RFLOAT> oversampled_rot, oversampled_tilt, oversampled_psi;

    CudaGlobalPtr<XFLOAT> alphas(nr_dir * nr_psi * nr_oversampled_rot * 9, eulers.getAllocator());
    CudaGlobalPtr<XFLOAT> betas (nr_dir * nr_psi * nr_oversampled_rot * 9, eulers.getAllocator());
    CudaGlobalPtr<XFLOAT> gammas(nr_dir * nr_psi * nr_oversampled_rot * 9, eulers.getAllocator());

    CudaGlobalPtr<XFLOAT> perturb(9, eulers.getAllocator());

    eulers.free_if_set();
    eulers.setSize(nr_dir * nr_psi * nr_oversampled_rot * 9);
    eulers.host_alloc();

    iorientclasses.free_if_set();
    iorientclasses.setSize(nr_dir * nr_psi * nr_oversampled_rot);
    iorientclasses.host_alloc();

    orientation_num = 0;

    RFLOAT myperturb = 0.0;

    if (abs(sampling.random_perturbation) > 0.0) {
        myperturb = sampling.random_perturbation * sampling.getAngularSampling();
        if (sampling.is_3D) {
            Matrix<RFLOAT> R = Euler::angles2matrix(myperturb, myperturb, myperturb);
            for (int i = 0; i < 9; i ++)
                perturb[i] = (XFLOAT) R.data()[i];
            perturb.put_on_device();
        }
    }

    RFLOAT pdf_orientation;
    TICTOC(TIMING_SAMPLING, ({
    for (long int idir = idir_min, iorient = 0; idir <= idir_max; idir++) {
        for (long int ipsi = ipsi_min, ipart = 0; ipsi <= ipsi_max; ipsi++, iorient++) {
            long int iorientclass = iclass * nr_dir * nr_psi + iorient;

            TICTOC(TIMING_PRIOR, ({
            // Get prior for this direction and skip calculation if prior == 0
            if (do_skip_align || do_skip_rotate) {
                pdf_orientation = pdf_class[iclass];
            } else if (orientational_prior_mode == NOPRIOR) {
                pdf_orientation = pdf_direction[iclass][idir];
            } else {
                pdf_orientation = directions_prior[idir] * psi_prior[ipsi];
            }
            }));

            // In the first pass, always proceed
            // In the second pass, check whether one of the translations for this orientation of any of the particles had a significant weight in the first pass
            // if so, proceed with projecting the reference in that direction

            bool do_proceed(false);

            TICTOC(TIMING_PROC_CALC, ({
            if (coarse && pdf_orientation > 0.0) {
                do_proceed = true;
            } else if (pdf_orientation > 0.0) {
                long int nr_trans = itrans_max - itrans_min + 1;
                for (long int ipart = 0; ipart < Ysize(*Mcoarse_significant); ipart++) {
                    long int ihidden = iorient * nr_trans;
                    for (long int itrans = itrans_min; itrans <= itrans_max; itrans++, ihidden++) {
                        if (direct::elem(*Mcoarse_significant, ipart, ihidden)) {
                            do_proceed = true;
                            break;
                        }
                    }
                }
            }
            }));

            TICTOC(TIMING_PROC, ({
            if (do_proceed) {
                // Now get the oversampled (rot, tilt, psi) triplets
                // This will be only the original (rot, tilt, psi) triplet in the first pass (sp.current_oversampling == 0)
                TICTOC(TIMING_GEN, ({
                getOrientations(
                    sampling, idir, ipsi, current_oversampling, oversampled_rot, oversampled_tilt, oversampled_psi,
                    pointer_dir_nonzeroprior, directions_prior, pointer_psi_nonzeroprior, psi_prior
                );
                }));

                // Loop over all oversampled orientations (only a single one in the first pass)
                for (long int iover_rot = 0; iover_rot < nr_oversampled_rot; iover_rot++, ipart++) {
                    if (sampling.is_3D) {
                        alphas[orientation_num] = oversampled_rot[iover_rot];
                        betas[orientation_num]  = oversampled_tilt[iover_rot];
                        gammas[orientation_num] = oversampled_psi[iover_rot];
                    } else {
                        alphas[orientation_num] = oversampled_psi[iover_rot] + myperturb;
                    }

                    iorientclasses[orientation_num] = iorientclass;
                    orientation_num ++;
                }
            }
            }));
        }
    }
    }));

    iorientclasses.setSize(orientation_num);
    iorientclasses.put_on_device();

    eulers.setSize(orientation_num * 9);
    eulers.device_alloc();

    alphas.setSize(orientation_num);
    alphas.put_on_device();

    if (sampling.is_3D) {
        betas.setSize(orientation_num);
        betas.put_on_device();
        gammas.setSize(orientation_num);
        gammas.put_on_device();
    }

    int grid_size = ceil((float) orientation_num / (float) BLOCK_SIZE);

    if (sampling.is_3D) {
        if (abs(sampling.random_perturbation) > 0.0) {
            if (inverseMatrix) {
                cuda_kernel_make_eulers_3D<true, true><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                    ~alphas, ~betas, ~gammas, ~eulers, orientation_num, ~perturb
                );
            } else {
                cuda_kernel_make_eulers_3D<false, true><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                    ~alphas, ~betas, ~gammas, ~eulers, orientation_num, ~perturb
                );
            }
        } else {
            if (inverseMatrix) {
                cuda_kernel_make_eulers_3D<true, false><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                    ~alphas, ~betas, ~gammas, ~eulers, orientation_num, NULL
                );
            } else {
                cuda_kernel_make_eulers_3D<false, false><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                    ~alphas, ~betas, ~gammas, ~eulers, orientation_num, NULL
                );
            }
        }
    } else {
        if (inverseMatrix) {
            cuda_kernel_make_eulers_2D<true><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                ~alphas, ~eulers, orientation_num
            );
        } else {
            cuda_kernel_make_eulers_2D<false><<<grid_size, BLOCK_SIZE, 0, eulers.getStream()>>>(
                ~alphas, ~eulers, orientation_num
            );
        }
    }
    }));
}


void CudaProjectorPlan::printTo(std::ostream &os) {
    // print
    os << "orientation_num = " << orientation_num << std::endl;
    os << "iorientclasses.size = " << iorientclasses.getSize() << std::endl;
    os << std::endl << "iorientclasses\tiover_rots\teulers" << std::endl;

    for (int i = 0; i < iorientclasses.getSize(); i++) {
        os << iorientclasses[i] << "\t\t" << "\t";
        for (int j = 0; j < 9; j++) {
            os << eulers[i * 9 + j] << "\t";
        }
        os << std::endl;
    }
}


void CudaProjectorPlan::clear() {
    orientation_num = 0;
    iorientclasses.free_if_set();
    iorientclasses.setSize(0);
    eulers.free_if_set();
    eulers.setSize(0);
    #ifdef PP_TIMING
    timer.printTimes(false);
    #endif
}
