#ifndef DIFF_CTF_OPTIMISATION
#define DIFF_CTF_OPTIMISATION

#include <src/jaz/optimization/optimization.h>
#include <src/image.h>
#include <src/jaz/gravis/t2Matrix.h>
#include <src/jaz/obs_model.h>
#include <vector>

class ModularCtfOptimisation: public DifferentiableOptimization {

    public:

    typedef enum {
        PerParticle,
        PerMicrograph,
        Fixed,
        ModeCount
    } Mode;

    typedef enum {
        Phase,
        Defocus,
        Astigmatism1,
        Astigmatism2,
        SphericalAberration,
        BFactor,
        ScaleFactor,
        CtfParamCount
    } CtfParam;

    ModularCtfOptimisation(
        MetaDataTable &mdt,
        ObservationModel *obsModel,
        const std::vector<Image<Complex>> &obs,
        const std::vector<Image<Complex>> &pred,
        const std::vector<Image<RFLOAT>> &frqWghByGroup,
        std::string modeStr,
        int num_treads
    );
    /** About modeStr: 
     * five characters (from {p,m,f}) indicating whether
     * the phase, defocus, astigmatism, Cs and B/k (in this order)
     * are to be estimated per [p]article, per [m]icrograph or
     * to be kept [f]ixed.
     */

    double f(const std::vector<double> &x) const;
    double f(const std::vector<double> &x, void *tempStorage) const;

    void grad(const std::vector<double> &x, std::vector<double> &gradDest) const;
    void grad(const std::vector<double> &x, std::vector<double> &gradDest, void *tempStorage) const;

    void* allocateTempStorage() const;
    void deallocateTempStorage(void* ts) const;

    std::vector<double> encodeInitial();
    void writeToTable(const std::vector<double> &x);

    static bool validateModeString(std::string mode);
    static std::vector<Mode> decodeModes(std::string s);

    protected:

    MetaDataTable &mdt;
    ObservationModel *obsModel;
    const std::vector<Image<Complex>> &obs;
    const std::vector<Image<Complex>> &pred;

    int particle_count, param_count, num_treads;

    std::vector<Mode> modes;
    double paramScale[CtfParamCount];

    std::vector<double> initialValues, angpix;
    int paramOffset[CtfParamCount], paramParticleStep[CtfParamCount];

    std::vector<Image<RFLOAT>> aberrationByGroup;
    const std::vector<Image<RFLOAT>> &frqWghByGroup;


    inline double readParam(CtfParam param, const std::vector<double> &x, int p) const;

};

inline double ModularCtfOptimisation::readParam(
    CtfParam param, const std::vector<double> &x, int p
) const {
    if (modes[param] == Fixed) {
        return initialValues[CtfParamCount * p + param];
    } else {
        return x[paramOffset[param] + p * paramParticleStep[param]];
    }
}

#endif
