
#include <unistd.h>
#include <string.h>
#include <fstream>

#include <src/args.h>
#include <src/image.h>
#include <src/metadata_table.h>
#include <src/jaz/img_proc/filter_helper.h>
#include <src/jaz/stack_helper.h>
#include <src/jaz/obs_model.h>
#include <src/jaz/image_log.h>
#include <src/jaz/new_ft.h>
#include <src/jaz/noise_helper.h>
#include <src/jaz/fftw_helper.h>
#include <src/jaz/ctf_helper.h>

#include <omp.h>

using namespace gravis;

int main(int argc, char *argv[]) {
    std::string starFn, outPath, name;
    int threads, s, optGroup;
    double rad, maxFreqAng, minFreqAng;
    bool allParts;

    IOParser parser;

    try {
        parser.setCommandLine(argc, argv);

        parser.addSection("General options");

        starFn = parser.getOption("--i", "Input particle *.star file");
        rad = textToDouble(parser.getOption("--rad", "Particle radius [Å]"));
        optGroup = textToInteger(parser.getOption("--og", "Optics group", "1")) - 1;
        maxFreqAng = textToDouble(parser.getOption("--max_freq", "Max. image frequency [Å] (default is Nyquist)", "-1"));
        minFreqAng = textToDouble(parser.getOption("--min_freq", "Min. image frequency [Å]", "0"));
        name = parser.getOption("--name", "Name of dataset (for the plot)", "");
        allParts = parser.checkOption("--all_part", "Consider all particles, instead of only the first one in each micrograph");
        s = textToInteger(parser.getOption("--s", "Square size for estimation", "256"));
        threads = textToInteger(parser.getOption("--j", "Number of threads", "1"));
        outPath = parser.getOption("--o", "Output path");

        parser.checkForErrors();
    } catch (RelionError XE) {
        parser.writeUsage(std::cout);
        std::cerr << XE;
        return RELION_EXIT_FAILURE;
    }

    ObservationModel obsModel;
    MetaDataTable mdt0;

    ObservationModel::loadSafely(starFn, obsModel, mdt0);

    std::vector<MetaDataTable> allMdts = StackHelper::splitByMicrographName(mdt0);

    const int sh = s / 2 + 1;

    const double angpix = obsModel.getPixelSize(optGroup);

    if (maxFreqAng < 0) { maxFreqAng = 2 * angpix; }

    const double r2max = 1.0 / (maxFreqAng * maxFreqAng);
    const double r2min = minFreqAng > 0 ? 1.0 / (minFreqAng * minFreqAng) : -1;

    const int radPx = (int) (rad / angpix + 0.5);

    const int maxBin = 5 * s;

    const double as = s * angpix;

    std::vector<double> histCent(maxBin, 0.0);
    std::vector<double> histWorst(maxBin, 0.0);

    for (int m = 0; m < allMdts.size(); m++) {
        const int pc = allMdts[m].size();

        const double mgContrib = allParts ? 1.0 : pc;
        const int p_max = allParts ? pc : 1;

        for (int p = 0; p < p_max; p++) {
            int ogp = obsModel.getOpticsGroup(allMdts[m], p);

            if (ogp != optGroup) continue;

            CTF ctf = CtfHelper::makeCTF(allMdts[m], &obsModel, p);
            int opticsGroup = allMdts[m].getValue<int>(EMDL::IMAGE_OPTICS_GROUP, p) - 1;

            for (int y = 0; y < s;  y++)
            for (int x = 0; x < sh; x++) {
                double xx = x / as;
                double yy = y < sh ? y / as : (y - s) / as;

                const double r2 = xx * xx + yy * yy;

                if (r2 > r2max || r2 < r2min) continue;

                obsModel.magnify(xx, yy, obsModel.getMagMatrix(opticsGroup));
                t2Vector<RFLOAT> delocCent = RFLOAT(1.0 / (2 * angpix * PI)) * ctf.getGammaGrad(xx,yy);

                double delocCentVal = delocCent.normLInf();

                int sic = delocCentVal + 0.5;
                if (sic >= maxBin) { sic = maxBin - 1; }

                histCent[sic] += mgContrib;


                d2Vector delocWorst(std::abs(delocCent.x) + radPx, std::abs(delocCent.y) + radPx);

                double delocWorstVal = delocWorst.normLInf();

                int siw = delocWorstVal + 0.5;
                if (siw >= maxBin) { siw = maxBin - 1; }

                histWorst[siw] += mgContrib;
            }
        }
    }

    std::vector<double> histCentCumul(maxBin, 0.0), histWorstCumul(maxBin, 0.0);
    double cumulC = 0.0, cumulW = 0.0;

    int first = -1;

    for (int b = maxBin - 1; b >= 0; b--) {
        cumulC += histCent[b];
        histCentCumul[b] = cumulC;

        cumulW += histWorst[b];
        histWorstCumul[b] = cumulW;

        if (first < 0 && cumulW > 0.0) { first = b; }
    }

    if (first < 0) {
        std::cerr << "No data found!\n";
        return RELION_EXIT_FAILURE;
    }


    CPlot2D plot2D("");

    std::string title = "Delocalisation";
    if (!name.empty())
    title.append(" for " + name + " (opt. gr. " + std::to_string(optGroup + 1) + ")");
    title.append(" at " + std::to_string(angpix) + " A/px");
    title.append((minFreqAng <= 0 ? " (up to " : " (" + std::to_string(minFreqAng) + " A - ") + std::to_string(maxFreqAng) + " A)");

    plot2D.SetTitle(title);
    plot2D.SetDrawLegend(true);

    CDataSet center;
    center.SetDrawMarker(false);
    center.SetDatasetColor(0.0,0.0,0.0);
    center.SetLineWidth(0.5);
    center.SetDatasetTitle("particle center");

    CDataSet edge;
    edge.SetDrawMarker(false);
    edge.SetDatasetColor(0.3,0.3,0.6);
    edge.SetLineWidth(0.5);
    edge.SetDatasetTitle("worst periphery point (radius " + std::to_string(rad) + " A)");

    for (int i = 0; i < first + radPx && i < maxBin && i <= first + 1; i++) {
        center.AddDataPoint(CDataPoint(2 * i, histCentCumul[i] / histCentCumul[0]));
        edge.AddDataPoint(CDataPoint(2 * i, histWorstCumul[i] / histWorstCumul[0]));
    }

    plot2D.AddDataSet(center);
    plot2D.AddDataSet(edge);

    plot2D.SetXAxisTitle("box size (pixels)");
    plot2D.SetYAxisTitle("fraction of pixels outside of box");

    plot2D.OutputPostScriptPlot(outPath + ".eps");

    return RELION_EXIT_SUCCESS;
}
