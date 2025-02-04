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
#include "src/pipeliner.h"
using std::string;

static string errorMsg(string s) {
    return "ERROR: " + s;
}

static string flankXXX(string s) {
    return "XXX" + s + "XXX";
}

FileName getTheOtherHalf(const FileName &fn_half1) {
    FileName fn_half2 = fn_half1.afterLastOf("/");

    if (fn_half2.contains("half1")) {
        fn_half2.replaceAllSubstrings("half1", "half2");
    } else if (fn_half2.contains("half2")) {
        fn_half2.replaceAllSubstrings("half2", "half1");
    } else {
        throw "File name does not contain 'half1' / 'half2'!";
    }

    if (fn_half1.contains("/"))
        fn_half2 = fn_half1.beforeLastOf("/") + "/" + fn_half2;

    return fn_half2;
}

vector<Node> getOutputNodesRefine(
    string outputname, int iter, int K, int dim, int nr_bodies
) {
    vector<Node> result;

    if (2 < dim || dim > 3)
    REPORT_ERROR("getOutputNodesRefine " + errorMsg("invalid dim value"));

    FileName fn_out = iter < 0 ?
        outputname :  // 3D auto-refine
        FileName::compose(outputname + "_it", iter, "", 3);  // 2D or 3D classification

    // Data and model.star files
    if (nr_bodies > 1) {
        for (int ibody = 0; ibody < nr_bodies; ibody++) {
            const auto fn_tmp = FileName::compose(fn_out + "_half1_body", ibody + 1, "", 3) + "_unfil.mrc";
            result.emplace_back(fn_tmp, Node::HALFMAP);
        }
    } else {
        // normal refinements/classifications
        result.emplace_back(fn_out + "_data.star", Node::PART_DATA);

        if (iter > 0) {
            // For classifications: output node model.star to make selections
            result.emplace_back(fn_out + "_model.star", Node::MODEL);
        } else {
            // For auto-refine: also output the run_half1_class001_unfil.mrc map
            result.emplace_back(fn_out + "_half1_class001_unfil.mrc", Node::HALFMAP);
        }

        // For 3D classification or 3D auto-refine, also use individual 3D maps as outputNodes
        if (dim == 3) {
            for (int iclass = 0; iclass < K; iclass++) {
                const auto fn_tmp = FileName::compose(fn_out + "_class", iclass + 1, "mrc", 3);
                result.emplace_back(fn_tmp, Node::REF3D);
            }
        }
    }

    return result;
}

// Any constructor
JobOption::JobOption(const string &label, const string &default_value, const string &helptext) {
    clear();
    initialise(label, default_value, helptext);
    joboption_type = JOBOPTION::ANY;
}

// FileName constructor
JobOption::JobOption(
    const string &label, const string &default_value, const string &pattern, const string &directory,
    const string &helptext
) {
    clear();
    initialise(label, default_value, helptext);
    joboption_type = JOBOPTION::FILENAME;
    this->pattern = pattern;
    this->directory = directory;
}

// InputNode constructor
JobOption::JobOption(
    const string &label, int nodetype, const string &default_value, const string &pattern,
    const string &helptext
) {
    clear();
    initialise(label, default_value, helptext);
    joboption_type = JOBOPTION::INPUTNODE;
    this->pattern = pattern;
    this->node_type = nodetype;
}

// Radio constructor
JobOption::JobOption(
    const string &label, const vector<string> &radio_options, int ioption, const string &helptext
) {
    clear();
    this->radio_options = radio_options;

    const string defaultval = radio_options[ioption];

    initialise(label, defaultval, helptext);
    joboption_type = JOBOPTION::RADIO;
}

// Boolean constructor
JobOption::JobOption(const string &label, bool boolvalue, const string &helptext) {
    clear();
    const string default_value = boolvalue ? "Yes" : "No";
    initialise(label, default_value, helptext);
    joboption_type = JOBOPTION::BOOLEAN;
}

// Slider constructor
JobOption::JobOption(
    const string &label, float default_value, float min_value, float max_value, float step_value,
    const string &helptext
) {
    clear();
    initialise(label, floatToString(default_value), helptext);
    joboption_type = JOBOPTION::SLIDER;
    this->min_value  = min_value;
    this->max_value  = max_value;
    this->step_value = step_value;
}

void JobOption::writeToMetaDataTable(MetaDataTable& MD) const {
    const long int i = MD.addObject();
    MD.setValue(EMDL::JOBOPTION_VARIABLE, variable, i);
    MD.setValue(EMDL::JOBOPTION_VALUE,    value,    i);
}

void JobOption::clear() {
    label = value = default_value = helptext = label_gui = pattern = directory = "undefined";
    joboption_type = JOBOPTION::UNDEFINED;
    radio_options = job_undefined_options;
    node_type = min_value = max_value = step_value = 0.0;
}

void JobOption::initialise(
    const string &label, const string &default_value, const string &helptext
) {
    label_gui = this->label = label;
    value = this->default_value = default_value;
    this->helptext = helptext;
}

bool JobOption::isSchedulerVariable() {
    return value.find("$$") != string::npos;
}

string JobOption::getString() {
    return value;
}

void JobOption::setString(const string &newvalue) {
    value = newvalue;
}

int JobOption::getHealPixOrder(const string &s) {
    // Index in job_sampling_options
    for (int i = 0; i < 9; i++) {
        if (s == job_sampling_options[i])
            return i + 1;
    }
    return -1;
}

string JobOption::getCtfFitString(const string &s) {
    return
    s == job_ctffit_options[0] ? "f" :
    s == job_ctffit_options[1] ? "m" :
    s == job_ctffit_options[2] ? "p" :
    "";
}

float JobOption::getNumber() {

    if (value.substr(0, 2) == "$$") return 0;

    if (!&value) throw "Error in textToFloat of " + value;

    float retval;
    if (sscanf(value.c_str(), "%f", &retval)) {
        return retval;
    } else {
        throw "Error in textToFloat of " + value;
    }
}

bool JobOption::getBoolean() {
    if (joboption_type != JOBOPTION::BOOLEAN) {
        std::cerr << " joboption_type= " << joboption_type << " label= " << label << " value= " << value << std::endl;
        REPORT_ERROR(errorMsg("this JobOption does not return a boolean: " + label));
    }
    return value == "Yes";
}

bool JobOption::readValue(std::ifstream &in) {
    if (label.empty()) return false;

    string sought = label;
    if (label == "Estimate beamtilt?") {
        sought = "Perform beamtilt estimation?";
        // 3.0 compatibility
        // Wouldn't label = "Perform beamtilt estimation?" be more elegant?
    } else if (label == "Perform MTF correction?") {
        std::cerr << "A legacy job option \"Perform MTF correction?\" is ignored. If an MTF file name is supplied, MTF correction will be applied." << std::endl;
        return false;
    }

    // Start reading the ifstream at the top
    in.clear(); // reset eof if happened...
    in.seekg(0, std::ios::beg);
    string line;
    while (getline(in, line, '\n')) {
        if (line.rfind(sought) == 0) {
            // Label found
            int eqpos = line.rfind("==");
            value = line.substr(eqpos + 3, line.length() - eqpos - 3);
            return true;
        }
    }
    return false;
}

void JobOption::writeValue(std::ostream &out) {
    out << label << " == " << value << std::endl;
}

bool RelionJob::containsLabel(const string &label, string &option) {
    for (const auto &pair : joboptions) {
        if (pair.second.label == label) {
            option = pair.first;
            return true;
        }
    }
    return false;
}

void RelionJob::setOption(const string &setOptionLine) {
    std::size_t i = setOptionLine.find("==");
    if (i == string::npos)
    REPORT_ERROR(" " + errorMsg("no '==' on JobOptionLine: " + setOptionLine));

    // label == value
    const string label = setOptionLine.substr(0, i - 1);
    const string value = setOptionLine.substr(i + 3, setOptionLine.length() - (i + 3));
    // Surely this should be:
    // value = setOptionLine.substr(i + 3, setOptionLine.length() + 1);

    string option;  // For containsLabel
    if (joboptions.find(label) != joboptions.end()) {
        joboptions[label].setString(value);
    } else if (containsLabel(label, option)) {
        joboptions[option].setString(value);
    } else {
        REPORT_ERROR(" " + errorMsg("Job does not contain label: " + label));
    }
}

bool RelionJob::read(const string &fn, bool &is_continue, bool do_initialise) {
    // If fn is empty, use the hidden name
    FileName myfilename = fn.empty() ? hidden_name : fn;
    bool have_read = false;

    // For backwards compatibility
    if (!exists(myfilename + "job.star") && exists(myfilename + "run.job")) {
        std::ifstream fh((myfilename + "run.job").c_str(), std::ios_base::in);
        if (fh.fail())
        REPORT_ERROR("ERROR reading file: " + myfilename + "run.job");

        // Get job type from first line
        string line;
        getline(fh, line, '\n');
        size_t i = line.find("==") + 1;

        type = (int) textToFloat(line.substr(i + 1, line.length() - i).c_str());

        // Get is_continue from second line
        getline(fh, line, '\n');
        is_continue = this->is_continue = line.rfind("is_continue == true") == 0;

        if (do_initialise) initialise(type);

        // Read in all the stored options
        bool read_all = true;
        for (std::pair<const string, JobOption> &pair : joboptions) {
            if (!pair.second.readValue(fh))
                read_all = false;
        }
        // Do we want to do anything with read_all?
        have_read = true;

    }

    if (!have_read) {
        // Read from STAR

        FileName fn_star = myfilename;
        if (fn_star.getExtension() != "star" || !exists(fn_star)) {
            // full name was given
            fn_star += "job.star"; // "Refine3D/job123" OR ".gui_auto3d"
            if (!exists(fn_star))
                return false;
        }

        MetaDataTable MDhead;
        MDhead.read(fn_star, "job");
        const long int i = MDhead.size() - 1;
        type = MDhead.getValue<int>(EMDL::JOB_TYPE, i);
        is_continue = this->is_continue = MDhead.getValue<bool>(EMDL::JOB_IS_CONTINUE, i);
        if (do_initialise)
        initialise(type);

        MetaDataTable MDvals;
        MDvals.read(fn_star, "joboptions_values");
        for (long int i : MDvals) {
            const string label = MDvals.getValue<string>(EMDL::JOBOPTION_VARIABLE, i);
            if (joboptions.find(label) == joboptions.end()) {
                std::cerr << "WARNING: cannot find " << label << " in the defined joboptions. Ignoring it ..." << std::endl;
            } else {
                joboptions[label].value = MDvals.getValue<string>(EMDL::JOBOPTION_VALUE, i);
            }
        }
        have_read = true;
    }

    if (!have_read) return false;

    // Just check that went OK
    const std::vector<int> types {
        Process::IMPORT,
        Process::MOTIONCORR,
        Process::CTFFIND,
        Process::MANUALPICK,
        Process::AUTOPICK,
        Process::EXTRACT,
        Process::CLASSSELECT,
        Process::CLASS2D,
        Process::CLASS3D,
        Process::AUTO3D,
        Process::MULTIBODY,
        Process::MASKCREATE,
        Process::JOINSTAR,
        Process::SUBTRACT,
        Process::POST,
        Process::RESMAP,
        Process::INIMODEL,
        Process::MOTIONREFINE,
        Process::CTFREFINE,
        Process::EXTERNAL,
    };

    if (std::find(std::begin(types), std::end(types), type) == std::end(types)); {
        // If type isn't recognised
        REPORT_ERROR(errorMsg("cannot find correct job type in " + myfilename + "run.job, with type= " + integerToString(type)));
    }

    return true;
}


void RelionJob::write(const string &fn) {
    // If fn is empty, use the hidden name
    const FileName myfilename = fn.empty() ? hidden_name : fn;

    /* In 3.1, no longer write run.job, just keep reading run,job for backwards compatibility
     *
    std::ofstream fh;
    fh.open((myfilename+"run.job").c_str(), std::ios::out);
    if (!fh)
        REPORT_ERROR("ERROR: Cannot write to file: " + myfilename + "run.job");

    // Write the job type
    fh << "job_type == " << type << std::endl;

    // is_continue flag
    if (is_continue)
        fh << "is_continue == true" << std::endl;
    else
        fh << "is_continue == false" << std::endl;

    for (std::map<string,JobOption>::iterator it=joboptions.begin(); it!=joboptions.end(); ++it)
    {
        (it->second).writeValue(fh);
    }

    fh.close();
     */

    // Also write 3.1-style STAR file
    std::ofstream fh ((myfilename + "job.star").c_str(), std::ios::out);
    if (!fh)
    REPORT_ERROR(errorMsg("Cannot write to file: " + myfilename + "job.star"));

    MetaDataTable MDhead;
    MDhead.name = "job";
    MDhead.isList = true;
    const long int i = MDhead.addObject();
    MDhead.setValue(EMDL::JOB_TYPE, type, i);
    MDhead.setValue(EMDL::JOB_IS_CONTINUE, is_continue, i);
    // TODO: add name for output directories!!! make a std:;map between type and name for all options!
    // MDhead.setValue(EMDL::JOB_TYPE_NAME, type, i);
    MDhead.write(fh);

    // Now make a table with all the values
    MetaDataTable MDvals;
    for (const auto &option : joboptions) {
        option.second.writeToMetaDataTable(MDvals);
    }
    MDvals.name = "joboptions_values";
    MDvals.write(fh);
}

void RelionJob::saveJobSubmissionScript(
    const string &newfilename, const string &outputname, const vector<string> &commands
) {
    // Open the standard job submission file
    // Return true unless any of the following fails, in which case return false:
    // - reading from the template submission script
    // - writing to the job submission script
    // - finding nr_mpi, nr_threads, min_dedicated
    const FileName fn_qsub = joboptions["qsubscript"].getString();

    std::ifstream fh (fn_qsub.c_str(), std::ios_base::in);
    if (fh.fail())
    throw "Error reading template submission script in: " + fn_qsub;

    std::ofstream fo (newfilename.c_str(), std::ios::out);
    if (fo.fail())
    throw "Error writing to job submission script in: " + newfilename;

    // These getNumber() calls may throw
    int nmpi = joboptions.find("nr_mpi")     != joboptions.end() ? joboptions["nr_mpi"].getNumber() : 1;
    int nthr = joboptions.find("nr_threads") != joboptions.end() ? joboptions["nr_threads"].getNumber() : 1;
    int ncores = nmpi * nthr;
    int ndedi = joboptions["min_dedicated"].getNumber();
    float fnodes = (float) ncores / (float) ndedi;
    int nnodes = ceil(fnodes);

    if (fmod(fnodes, 1) > 0) {
        std::cout << "\n";
        std::cout << " Warning! You're using " << nmpi << " MPI processes with " << nthr << " threads each (i.e. " << ncores << " cores), while asking for " << nnodes << " nodes with " << ndedi << " cores.\n";
        std::cout << " It is more efficient to make the number of cores (i.e. mpi*threads) a multiple of the minimum number of dedicated cores per node " << std::endl;
    }

    fh.clear(); // reset eof if happened...
    fh.seekg(0, std::ios::beg);
    std::map<string, string> replacing;

    replacing[flankXXX("mpinodes")] = floatToString(nmpi);
    replacing[flankXXX("threads")] = floatToString(nthr);
    replacing[flankXXX("cores")] = floatToString(ncores);
    replacing[flankXXX("dedicated")] = floatToString(ndedi);
    replacing[flankXXX("nodes")] = floatToString(nnodes);
    replacing[flankXXX("name")] = outputname;
    replacing[flankXXX("errfile")] = outputname + "run.err";
    replacing[flankXXX("outfile")] = outputname + "run.out";
    replacing[flankXXX("queue")] = joboptions["queuename"].getString();
    char *extra_count_text = getenv("RELION_QSUB_EXTRA_COUNT");
    const char extra_count_val = extra_count_text ? atoi(extra_count_text) : 2;
    for (int i = 1; i <= extra_count_val; i++) {
        std::stringstream out;
        out << i;
        const string i_str = out.str();
        if (joboptions.find(string("qsub_extra") + i_str) != joboptions.end()) {
            replacing[string("XXX") + "extra" + i_str + "XXX"] = joboptions[string("qsub_extra") + i_str].getString();
        }
    }

    string line;
    while (getline(fh, line, '\n')) {
        // Replace all entries in the replacing map
        for (std::map<string,string>::iterator it = replacing.begin(); it != replacing.end(); ++it) {
            string subin  = it->first;
            string subout = it->second;
            // For each line, replace every occurrence of subin with subout
            size_t start_pos = 0;
            while ((start_pos = line.find(subin, start_pos)) != string::npos) {
                line.replace(start_pos, subin.length(), subout);
                start_pos += subout.length();
            }
        }

        if (line.find(flankXXX("command")) == string::npos) {
            fo << line << std::endl;
        } else {
            // Append the commands
            string ori_line = line;
            for (int icom = 0; icom < commands.size(); icom++) {
                // For multiple relion mpi commands: add multiple lines from the XXXcommandXXX template
                if (
                    commands[icom].find("relion_") != string::npos && (
                    commands[icom].find("_mpi`")   != string::npos || nmpi == 1
                    )
                ) {
                    // if there are no MPI programs, then still use XXXcommandXXX once
                    string from = flankXXX("command");
                    string to = commands[icom];
                    line.replace(line.find(from), from.length(), to);
                    fo << line << std::endl;
                    line = ori_line;
                } else {
                    // Just add the sequential command
                    fo << commands[icom] << std::endl;
                }
            }
        }
    }

    fo << std::endl;
}

void RelionJob::initialisePipeline(string &outputname, const string &defaultname, int job_counter) {
    outputNodes.clear();
    inputNodes.clear();

    if (outputname.empty()) {
        // for continue jobs, use the same outputname
        outputname = defaultname + "/job" + (job_counter < 1000 ?
            integerToString(job_counter, 3) :
            integerToString(job_counter)
        ) + "/";
    }

    outputName = outputname;
}

string RelionJob::prepareFinalCommand(
    const string &outputname, vector<string> &commands, bool do_makedir
) {

    // Create output directory if the outname contains a "/"
    if (do_makedir) {
        int last_slash = outputname.rfind("/");
        if (last_slash < outputname.size()) {
            string dirs = outputname.substr(0, last_slash);
            string makedirs = "mkdir -p " + dirs;
            int res = system(makedirs.c_str());
        }
    }

    // Add the --pipeline_control argument to all relion_ programs
    for (auto &command : commands) {
        if (command.find("relion_") != string::npos) {
            command += " --pipeline_control " + outputname;
        }
    }

    // Prepare full mpi commands or save jobsubmission script to disc
    if (joboptions["do_queue"].getBoolean() && do_makedir) {
        // Make the submission script and write it to disc
        string output_script = outputname + "run_submit.script";
        saveJobSubmissionScript(output_script, outputname, commands);  // May throw
        return joboptions["qsub"].getString() + " " + output_script + " &";
    }

    // Is this a relion mpi program?
    int nr_mpi = joboptions.find("nr_mpi") == joboptions.end() ? 1 : joboptions["nr_mpi"].getNumber();  // May throw

    for (auto &command : commands) {
        // Add mpirun in front of those commands that contain "relion_" and "_mpi`"
        // (if no submission via the queue is done)
        if (
            nr_mpi > 1 &&
            command.find("_mpi`")   != string::npos &&
            command.find("relion_") != string::npos
        ) {
            const char *mpirun = getenv("RELION_MPIRUN");
            if (!mpirun) { mpirun = DEFAULT::MPIRUN; }
            command = string(mpirun) + " -n " + floatToString(nr_mpi) + " " + command;
        }

        // Save stdout and stderr to a .out and .err files
        // But only when a re-direct '>' is NOT already present on the command line!
        if (command.find(">") == string::npos)
            command += " >> " + outputname + "run.out 2>> " + outputname + "run.err";
    }

    char *warning = getenv("RELION_ERROR_LOCAL_MPI");
    int nr_warn = warning ? textToInteger(warning) : DEFAULT::WARNINGLOCALMPI;

    if (nr_mpi > nr_warn && !joboptions["do_queue"].getBoolean())
    throw
        "You're submitting a local job with " + floatToString(nr_mpi) + " parallel MPI processes. "
        "That's more than allowed by the environment variable RELION_ERROR_LOCAL_MPI.";

    // Join the commands on a single line
    return join(commands, " && ") + " & ";
    // && execute each command only after the previous command succeeds
    // &  end by putting composite job in the background
}


void RelionJob::initialise(int job_type) {
    type = job_type;
    bool has_mpi, has_thread;
    switch (type) {

        case Process::IMPORT:
        has_mpi = has_thread = false;
        initialiseImportJob();
        break;

        case Process::MOTIONCORR:
        has_mpi = has_thread = true;
        initialiseMotioncorrJob();
        break;

        case Process::CTFFIND:
        has_mpi = !(has_thread = false);
        initialiseCtffindJob();
        break;

        case Process::MANUALPICK:
        has_mpi = has_thread = false;
        initialiseManualpickJob();
        break;

        case Process::AUTOPICK:
        has_mpi = !(has_thread = false);
        initialiseAutopickJob();
        break;

        case Process::EXTRACT:
        has_mpi = !(has_thread = false);
        initialiseExtractJob();
        break;

        case Process::CLASSSELECT:
        has_mpi = has_thread = false;
        initialiseSelectJob();
        break;

        case Process::CLASS2D:
        has_mpi = has_thread = true;
        initialiseClass2DJob();
        break;

        case Process::INIMODEL:
        has_mpi = has_thread = true;
        initialiseInimodelJob();
        break;

        case Process::CLASS3D:
        has_mpi = has_thread = true;
        initialiseClass3DJob();
        break;

        case Process::AUTO3D:
        has_mpi = has_thread = true;
        initialiseAutorefineJob();
        break;

        case Process::MULTIBODY:
        has_mpi = has_thread = true;
        initialiseMultiBodyJob();
        break;

        case Process::MASKCREATE:
        has_mpi = !(has_thread = true);
        initialiseMaskcreateJob();
        break;

        case Process::JOINSTAR:
        has_mpi = has_thread = false;
        initialiseJoinstarJob();
        break;

        case Process::SUBTRACT:
        has_mpi = !(has_thread = false);
        initialiseSubtractJob();
        break;

        case Process::POST:
        has_mpi = has_thread = false;
        initialisePostprocessJob();
        break;

        case Process::RESMAP:
        has_mpi = has_thread = true;
        initialiseLocalresJob();
        break;

        case Process::MOTIONREFINE:
        has_mpi = has_thread = true;
        initialiseMotionrefineJob();
        break;

        case Process::CTFREFINE:
        has_mpi = has_thread = true;
        initialiseCtfrefineJob();
        break;

        case Process::EXTERNAL:
        has_mpi = !(has_thread = true);
        initialiseExternalJob();
        break;

        default:
        REPORT_ERROR(errorMsg("unrecognised job type"));
    }


    // Check for environment variable RELION_MPI_MAX and RELION_QSUB_NRMPI
    const char *mpi_max_input = getenv("RELION_MPI_MAX");
    int mpi_max = mpi_max_input ? textToInteger(mpi_max_input) : DEFAULT::MPIMAX;
    char *qsub_nrmpi_text = getenv("RELION_QSUB_NRMPI");
    const char qsub_nrmpi_val = qsub_nrmpi_text ? atoi(qsub_nrmpi_text) : DEFAULT::NRMPI;
    if (has_mpi) {
        joboptions["nr_mpi"] = JobOption("Number of MPI procs:", qsub_nrmpi_val , 1, mpi_max, 1, "Number of MPI nodes to use in parallel. When set to 1, MPI will not be used. The maximum can be set through the environment variable RELION_MPI_MAX.");
    }

    const char *thread_max_input = getenv("RELION_THREAD_MAX");
    int thread_max = thread_max_input ? textToInteger(thread_max_input) : DEFAULT::THREADMAX;
    char *qsub_nrthr_text = getenv("RELION_QSUB_NRTHREADS");
    const char qsub_nrthreads_val = qsub_nrthr_text ? atoi(qsub_nrthr_text) : DEFAULT::NRTHREADS;
    if (has_thread) {
        joboptions["nr_threads"] = JobOption("Number of threads:", qsub_nrthreads_val, 1, thread_max, 1, "Number of shared-memory (POSIX) threads to use in parallel. \
When set to 1, no multi-threading will be used. The maximum can be set through the environment variable RELION_THREAD_MAX.");
    }

    const char *use_queue_input = getenv("RELION_QUEUE_USE");
    bool use_queue = use_queue_input ? textToBool(use_queue_input) : DEFAULT::QUEUEUSE;
    joboptions["do_queue"] = JobOption("Submit to queue?", use_queue, "If set to Yes, the job will be submit to a queue, otherwise \
the job will be executed locally. Note that only MPI jobs may be sent to a queue. The default can be set through the environment variable RELION_QUEUE_USE.");

    // Check for environment variable RELION_QUEUE_NAME
    const char *queue_name = getenv("RELION_QUEUE_NAME");
    if (!queue_name) { queue_name = DEFAULT::QUEUENAME; }

    // Need the string(), as otherwise it will be overloaded and passed as a boolean....
    joboptions["queuename"] = JobOption("Queue name: ", string(queue_name), "Name of the queue to which to submit the job. The default name can be set through the environment variable RELION_QUEUE_NAME.");

    // Check for environment variable RELION_QSUB_COMMAND
    const char *qsub_command = getenv("RELION_QSUB_COMMAND");
    if (!qsub_command) { qsub_command = DEFAULT::QSUBCOMMAND; }

    joboptions["qsub"] = JobOption("Queue submit command:", string(qsub_command), "Name of the command used to submit scripts to the queue, e.g. qsub or bsub.\n\n\
Note that the person who installed RELION should have made a custom script for your cluster/queue setup. Check this is the case \
(or create your own script following the RELION Wiki) if you have trouble submitting jobs. The default command can be set through the environment variable RELION_QSUB_COMMAND.");

    // additional options that may be set through environment variables RELION_QSUB_EXTRAi and RELION_QSUB_EXTRAi (for more flexibility)
    char *extra_count_text = getenv("RELION_QSUB_EXTRA_COUNT");
    const char extra_count_val = extra_count_text ? atoi(extra_count_text) : 2;
    for (int i = 1; i <= extra_count_val; i++) {
        const string i_str = std::to_string(i);
        char *extra_text = getenv((string("RELION_QSUB_EXTRA") + i_str).c_str());
        if (extra_text) {
            const string query_default = string("RELION_QSUB_EXTRA") + i_str + "_DEFAULT";
            char *extra_default = getenv(query_default.c_str());
            char emptychar[] = "";
            if (!extra_default) { extra_default = emptychar; }
            const string query_help = string("RELION_QSUB_EXTRA") + i_str + "_HELP";
            char *extra_help = getenv(query_help.c_str());
            string txt = extra_help ? string(extra_help) :
                (string("Extra option to pass to the qsub template script. Any occurrences of XXXextra") + i_str + "XXX will be changed by this value.");
            joboptions[string("qsub_extra") + i_str] = JobOption(string(extra_text), string(extra_default), txt.c_str());
        }
    }

    // Check for environment variable RELION_QSUB_TEMPLATE
    const char *qsub_template = getenv("RELION_QSUB_TEMPLATE");
    if (!qsub_template) { qsub_template = DEFAULT::QSUBLOCATION; }
    joboptions["qsubscript"] = JobOption("Standard submission script:", string(qsub_template), "Script Files (*.{csh,sh,bash,script})", ".",
"The template for your standard queue job submission script. \
Its default location may be changed by setting the environment variable RELION_QSUB_TEMPLATE. \
In the template script a number of variables will be replaced: \n \
XXXcommandXXX = relion command + arguments; \n \
XXXqueueXXX = The queue name; \n \
XXXmpinodesXXX = The number of MPI nodes; \n \
XXXthreadsXXX = The number of threads; \n \
XXXcoresXXX = XXXmpinodesXXX * XXXthreadsXXX; \n \
XXXdedicatedXXX = The minimum number of dedicated cores on each node; \n \
XXXnodesXXX = The number of requested nodes = ceil(XXXcoresXXX / XXXdedicatedXXX); \n \
If these options are not enough for your standard jobs, you may define a user-specified number of extra variables: XXXextra1XXX, XXXextra2XXX, etc. \
The number of extra variables is controlled through the environment variable RELION_QSUB_EXTRA_COUNT. \
Their help text is set by the environment variables RELION_QSUB_EXTRA1, RELION_QSUB_EXTRA2, etc \
For example, setenv RELION_QSUB_EXTRA_COUNT 1, together with setenv RELION_QSUB_EXTRA1 \"Max number of hours in queue\" will result in an additional (text) ein the GUI \
Any variables XXXextra1XXX in the template script will be replaced by the corresponding value.\
Likewise, default values for the extra entries can be set through environment variables RELION_QSUB_EXTRA1_DEFAULT, RELION_QSUB_EXTRA2_DEFAULT, etc. \
But note that (unlike all other entries in the GUI) the extra values are not remembered from one run to the other.");

    // Check for environment variable RELION_MINIMUM_DEDICATED
    char *my_minimum_dedicated = getenv("RELION_MINIMUM_DEDICATED");
    int minimum_nr_dedicated = my_minimum_dedicated ? textToInteger(my_minimum_dedicated) : DEFAULT::MINIMUMDEDICATED;
    joboptions["min_dedicated"] = JobOption("Minimum dedicated cores per node:", minimum_nr_dedicated, 1, 64, 1, "Minimum number of dedicated cores that need to be requested on each node. This is useful to force the queue to fill up entire nodes of a given size. The default can be set through the environment variable RELION_MINIMUM_DEDICATED.");

    // Need the string(), as otherwise it will be overloaded and passed as a boolean....
    joboptions["other_args"] = JobOption("Additional arguments:", string(""), "In this box command-line arguments may be provided that are not generated by the GUI. \
This may be useful for testing developmental options and/or expert use of the program. \
To print a list of possible options, run the corresponding program from the command line without any arguments.");

    // Set the variable name in all joboptions
    for (auto &option : joboptions) {
        option.second.variable = option.first;
    }
}

string RelionJob::getCommands(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    switch (type) {

        case Process::IMPORT:
        return getCommandsImportJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::MOTIONCORR:
        return getCommandsMotioncorrJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::CTFFIND:
        return getCommandsCtffindJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::MANUALPICK:
        return getCommandsManualpickJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::AUTOPICK:
        return getCommandsAutopickJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::EXTRACT:
        return getCommandsExtractJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::CLASSSELECT:
        return getCommandsSelectJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::CLASS2D:
        return getCommandsClass2DJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::INIMODEL:
        return getCommandsInimodelJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::CLASS3D:
        return getCommandsClass3DJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::AUTO3D:
        return getCommandsAutorefineJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::MULTIBODY:
        return getCommandsMultiBodyJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::MASKCREATE:
        return getCommandsMaskcreateJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::JOINSTAR:
        return getCommandsJoinstarJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::SUBTRACT:
        return getCommandsSubtractJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::POST:
        return getCommandsPostprocessJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::RESMAP:
        return getCommandsLocalresJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::MOTIONREFINE:
        return getCommandsMotionrefineJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::CTFREFINE:
        return getCommandsCtfrefineJob(
            outputname, commands, do_makedir, job_counter
        );

        case Process::EXTERNAL:
        return getCommandsExternalJob(
            outputname, commands, do_makedir, job_counter
        );

        default:
        REPORT_ERROR(errorMsg("unrecognised job type: type = " + integerToString(type)));
    }
}

void RelionJob::initialiseImportJob() {
    hidden_name = ".gui_import";

    joboptions["do_raw"] = JobOption("Import raw movies/micrographs?", true, "Set this to Yes if you plan to import raw movies or micrographs");
    joboptions["fn_in_raw"] = JobOption("Raw input files:", "Micrographs/*.tif", (string)"Movie or Image (*.{mrc,mrcs,tif,tiff})", ".", "Provide a Linux wildcard that selects all raw movies or micrographs to be imported. The path must be a relative path from the project directory. To import files outside the project directory, first make a symbolic link by an absolute path and then specify the link by a relative path. See the FAQ page on RELION wiki (https://www3.mrc-lmb.cam.ac.uk/relion/index.php/FAQs#What_is_the_right_way_to_import_files_outside_the_project_directory.3F) for details.");
    joboptions["is_multiframe"] = JobOption("Are these multi-frame movies?", true, "Set to Yes for multi-frame movies, set to No for single-frame micrographs.");

    joboptions["optics_group_name"] = JobOption("Optics group name:", (string)"opticsGroup1", "Name of this optics group. Each group of movies/micrographs with different optics characteristics for CTF refinement should have a unique name.");
    joboptions["fn_mtf"] = JobOption("MTF of the detector:", "", "STAR Files (*.star)", ".", "As of release-3.1, the MTF of the detector is used in the refinement stages of refinement.  \
If you know the MTF of your detector, provide it here. Curves for some well-known detectors may be downloaded from the RELION Wiki. Also see there for the exact format \
\n If you do not know the MTF of your detector and do not want to measure it, then by leaving this entry empty, you include the MTF of your detector in your overall estimated B-factor upon sharpening the map.\
Although that is probably slightly less accurate, the overall quality of your map will probably not suffer very much. \n \n Note that when combining data from different detectors, the differences between their MTFs can no longer be absorbed in a single B-factor, and providing the MTF here is important!");

    joboptions["angpix"] = JobOption("Pixel size (Angstrom):", 1.4, 0.5, 3, 0.1, "Pixel size in Angstroms. ");
    joboptions["kV"] = JobOption("Voltage (kV):", 300, 50, 500, 10, "Voltage the microscope was operated on (in kV)");
    joboptions["Cs"] = JobOption("Spherical aberration (mm):", 2.7, 0, 8, 0.1, "Spherical aberration of the microscope used to collect these images (in mm). Typical values are 2.7 (FEI Titan & Talos, most JEOL CRYO-ARM), 2.0 (FEI Polara), 1.4 (some JEOL CRYO-ARM) and 0.01 (microscopes with a Cs corrector).");
    joboptions["Q0"] = JobOption("Amplitude contrast:", 0.1, 0, 0.3, 0.01, "Fraction of amplitude contrast. Often values around 10% work better than theoretically more accurate lower values...");
    joboptions["beamtilt_x"] = JobOption("Beamtilt in X (mrad):", 0.0, -1.0, 1.0, 0.1, "Known beamtilt in the X-direction (in mrad). Set to zero if unknown.");
    joboptions["beamtilt_y"] = JobOption("Beamtilt in Y (mrad):", 0.0, -1.0, 1.0, 0.1, "Known beamtilt in the Y-direction (in mrad). Set to zero if unknown.");

    joboptions["do_other"] = JobOption("Import other node types?", false, "Set this to Yes  if you plan to import anything else than movies or micrographs");

    joboptions["fn_in_other"] = JobOption("Input file:", "ref.mrc", "Input file (*.*)", ".", "Select any file(s) to import. \n \n \
Note that for importing coordinate files, one has to give a Linux wildcard, where the *-symbol is before the coordinate-file suffix, e.g. if the micrographs are called mic1.mrc and the coordinate files mic1.box or mic1_autopick.star, one HAS to give '*.box' or '*_autopick.star', respectively.\n \n \
Also note that micrographs, movies and coordinate files all need to be in the same directory (with the same rootnames, e.g.mic1 in the example above) in order to be imported correctly. 3D masks or references can be imported from anywhere. \n \n \
Note that movie-particle STAR files cannot be imported from a previous version of RELION, as the way movies are handled has changed in RELION-2.0. \n \n \
For the import of a particle, 2D references or micrograph STAR file or of a 3D reference or mask, only a single file can be imported at a time. \n \n \
Note that due to a bug in a fltk library, you cannot import from directories that contain a substring  of the current directory, e.g. dont important from /home/betagal if your current directory is called /home/betagal_r2. In this case, just change one of the directory names.");

    joboptions["node_type"] = JobOption("Node type:", job_nodetype_options, 0, "Select the type of Node this is.");
    joboptions["optics_group_particles"] = JobOption("Rename optics group for particles:", (string)"", "Only for the import of a particles STAR file with a single, or no, optics groups defined: rename the optics group for the imported particles to this string.");
}

// Generate the correct commands
string RelionJob::getCommandsImportJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::IMPORT_NAME, job_counter);

    FileName fn_out, fn_in;
    string command = "relion_import ";

    bool do_raw = joboptions["do_raw"].getBoolean();
    bool do_other = joboptions["do_other"].getBoolean();

    if (do_raw && do_other)
    throw errorMsg("you cannot import BOTH raw movies/micrographs AND other node types at the same time...");

    if (!do_raw && !do_other)
    throw errorMsg("nothing to do... ");

    if (do_raw) {
        fn_in = joboptions["fn_in_raw"].getString();

        if (fn_in.rfind("../") != string::npos) {
            // Forbid at any place
            throw errorMsg("don't import files outside the project directory.\nPlease make a symbolic link by an absolute path before importing.");
        }

        if (fn_in.rfind("/", 0) == 0) {
            // Forbid only at the beginning
            throw errorMsg("please import files by a relative path.\nIf you want to import files outside the project directory, make a symbolic link by an absolute path and\nimport the symbolic link by a relative path.");
        }

        if (joboptions["is_multiframe"].getBoolean()) {
            fn_out = "movies.star";
            outputNodes.emplace_back(outputname + fn_out, Node::MOVIES);
            command += " --do_movies ";
        } else {
            fn_out = "micrographs.star";
            outputNodes.emplace_back(outputname + fn_out, Node::MICS);
            command += " --do_micrographs ";
        }

        FileName optics_group = joboptions["optics_group_name"].getString();
        if (optics_group.empty())
        throw errorMsg("please specify an optics group name.");

        if (!optics_group.validateCharactersStrict(true)) {
            // true means: do_allow_double_dollar (for scheduler)
            throw errorMsg("an optics group name may contain only alphanumeric characters and hyphen/minus (-).");
        }

        command += " --optics_group_name \"" + optics_group + "\"";
        if (!joboptions["fn_mtf"].getString().empty())
        command += " --optics_group_mtf " + joboptions["fn_mtf"].getString();
        command += " --angpix " + joboptions["angpix"].getString();
        command += " --kV " + joboptions["kV"].getString();
        command += " --Cs " + joboptions["Cs"].getString();
        command += " --Q0 " + joboptions["Q0"].getString();
        command += " --beamtilt_x " + joboptions["beamtilt_x"].getString();
        command += " --beamtilt_y " + joboptions["beamtilt_y"].getString();

    } else if (do_other) {
        fn_in = joboptions["fn_in_other"].getString();
        const string node_type = joboptions["node_type"].getString();
        if (node_type == "Particle coordinates (*.box, *_pick.star)") {
            // Make a suffix file, which contains the actual suffix as a suffix
            // Get the coordinate-file suffix
            outputNodes.emplace_back(outputname + "coords_suffix" + fn_in.afterLastOf("*"), Node::MIC_COORDS);
            command += " --do_coordinates ";
        } else {
            fn_out = FileName("/" + fn_in).afterLastOf("/");

            int node_type_i =
                node_type == "Particles STAR file (.star)"     ? Node::PART_DATA :
                node_type == "2D references (.star or .mrcs)"  ? Node::REFS2D :
                node_type == "3D reference (.mrc)"             ? Node::REF3D :
                node_type == "3D mask (.mrc)"                  ? Node::MASK :
                node_type == "Micrographs STAR file (.star)"   ? Node::MICS :
                node_type == "Unfiltered half-map (unfil.mrc)" ? Node::HALFMAP :
                -1;

            if (node_type_i < 0)
            throw "Unrecognized menu option for node_type = " + node_type;

            outputNodes.emplace_back(outputname + fn_out, node_type_i);

            // Also get the other half-map
            switch (node_type_i) {

                case Node::HALFMAP:
                {
                    FileName fn_inb = "/" + fn_in;
                    size_t pos = fn_inb.find("half1");
                    if (pos != string::npos) {
                        fn_inb.replace(pos, 5, "half2");
                    } else {
                        pos = fn_inb.find("half2");
                        if (pos != string::npos) {
                            fn_inb.replace(pos, 5, "half1");
                        }
                    }
                    fn_inb = fn_inb.afterLastOf("/");
                    outputNodes.emplace_back(outputname + fn_inb, node_type_i);
                    command += " --do_halfmaps";
                    break;
                }

                case Node::PART_DATA:
                {
                    command += " --do_particles";
                    FileName optics_group = joboptions["optics_group_particles"].getString();
                    if (!optics_group.empty()) {
                        if (!optics_group.validateCharactersStrict())
                        throw errorMsg("an optics group name may contain only alphanumeric characters and hyphens.");
                        command += " --particles_optics_group_name \"" + optics_group + "\"";
                    }
                    break;
                }

                default:
                command += " --do_other";
            }
        }
    }

    // Now finish the command call to relion_import program, which does the actual copying
    command += " --i \"" + fn_in + "\"";
    command += " --odir " + outputname;
    command += " --ofile " + fn_out;

    if (is_continue)
    command += " --continue ";

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseMotioncorrJob() {
    hidden_name = ".gui_motioncorr";

    joboptions["input_star_mics"] = JobOption("Input movies STAR file:", Node::MOVIES, "", "STAR files (*.star)", "A STAR file with all micrographs to run MOTIONCORR on");
    joboptions["first_frame_sum"] = JobOption("First frame for corrected sum:", 1, 1, 32, 1, "First frame to use in corrected average (starts counting at 1). ");
    joboptions["last_frame_sum"] = JobOption("Last frame for corrected sum:", -1, 0, 32, 1, "Last frame to use in corrected average. Values equal to or smaller than 0 mean 'use all frames'.");
    joboptions["eer_grouping"] = JobOption("EER fractionation:", 32, 1, 100, 1, "The number of hardware frames to group into one fraction. This option is relevant only for Falcon4 movies in the EER format. Note that all 'frames' in the GUI (e.g. first and last frame for corrected sum, dose per frame) refer to fractions, not raw detector frames. See https://www3.mrc-lmb.cam.ac.uk/relion/index.php/Image_compression#Falcon4_EER for detailed guidance on EER processing.");

    // Motioncor2

    // Check for environment variable RELION_MOTIONCOR2_EXECUTABLE
    const char *motioncor2_exe = getenv("RELION_MOTIONCOR2_EXECUTABLE");
    if (!motioncor2_exe) { motioncor2_exe = DEFAULT::MOTIONCOR2LOCATION; }

    // Common arguments RELION and UCSF implementation
    joboptions["bfactor"] = JobOption("Bfactor:", 150, 0, 1500, 50, "The B-factor that will be applied to the micrographs.");
    joboptions["patch_x"] = JobOption("Number of patches X:", string("1"), "Number of patches (in X and Y direction) to apply motioncor2.");
    joboptions["patch_y"] = JobOption("Number of patches Y:", string("1"), "Number of patches (in X and Y direction) to apply motioncor2.");
    joboptions["group_frames"] = JobOption("Group frames:", 1, 1, 5, 1, "Average together this many frames before calculating the beam-induced shifts.");
    joboptions["bin_factor"] = JobOption("Binning factor:", 1, 1, 2, 1, "Bin the micrographs this much by a windowing operation in the Fourier Tranform. Binning at this level is hard to un-do later on, but may be useful to down-scale super-resolution images. Float-values may be used. Do make sure though that the resulting micrograph size is even.");
    joboptions["fn_gain_ref"] = JobOption("Gain-reference image:", "", "*.mrc", ".", "Location of the gain-reference file to be applied to the input micrographs. Leave this empty if the movies are already gain-corrected.");
    joboptions["gain_rot"] = JobOption("Gain rotation:", job_gain_rotation_options, 0, "Rotate the gain reference by this number times 90 degrees clockwise in relion_display. This is the same as -RotGain in MotionCor2. Note that MotionCor2 uses a different convention for rotation so it says 'counter-clockwise'. Valid values are 0, 1, 2 and 3.");
    joboptions["gain_flip"] = JobOption("Gain flip:", job_gain_flip_options, 0, "Flip the gain reference after rotation. This is the same as -FlipGain in MotionCor2. 0 means do nothing, 1 means flip Y (upside down) and 2 means flip X (left to right).");

    // UCSF-wrapper
    joboptions["do_own_motioncor"] = JobOption("Use RELION's own implementation?", true , "If set to Yes, use RELION's own implementation of a MotionCor2-like algorithm by Takanori Nakane. Otherwise, wrap to the UCSF implementation. Note that Takanori's program only runs on CPUs but uses multiple threads, while the UCSF-implementation needs a GPU but uses only one CPU thread. Takanori's implementation is most efficient when the number of frames is divisible by the number of threads (e.g. 12 or 18 threads per MPI process for 36 frames). On some machines, setting the OMP_PROC::BIND environmental variable to TRUE accelerates the program.\n\
When running on 4k x 4k movies and using 6 to 12 threads, the speeds should be similar. Note that Takanori's program uses the same model as the UCSF program and gives results that are almost identical.\n\
Whichever program you use, 'Motion Refinement' is highly recommended to get the most of your dataset.");
    joboptions["fn_motioncor2_exe"] = JobOption("MOTIONCOR2 executable:", string(motioncor2_exe), "*.*", ".", "Location of the MOTIONCOR2 executable. You can control the default of this field by setting environment variable RELION_MOTIONCOR2_EXECUTABLE, or by editing the first few lines in src/gui_jobwindow.h and recompile the code.");
    joboptions["fn_defect"] = JobOption("Defect file:", "", "*", ".", "Location of a UCSF MotionCor2-style defect text file or a defect map that describe the defect pixels on the detector. Each line of a defect text file should contain four numbers specifying x, y, width and height of a defect region. A defect map is an image (MRC or TIFF), where 0 means good and 1 means bad pixels. The coordinate system is the same as the input movie before application of binning, rotation and/or flipping.\nNote that the format of the defect text is DIFFERENT from the defect text produced by SerialEM! One can convert a SerialEM-style defect file into a defect map using IMOD utilities e.g. \"clip defect -D defect.txt -f tif movie.mrc defect_map.tif\". See explanations in the SerialEM manual.\n\nLeave empty if you don't have any defects, or don't want to correct for defects on your detector.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string("0"), "Provide a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':'. For example, to place one rank on device 0 and one rank on device 1, provide '0:1'.\n\
Note that multiple MotionCor2 processes should not share a GPU; otherwise, it can lead to crash or broken outputs (e.g. black images) .");
    joboptions["other_motioncor2_args"] = JobOption("Other MOTIONCOR2 arguments", string(""), "Additional arguments that need to be passed to MOTIONCOR2.");

    // Dose-weight
    joboptions["do_dose_weighting"] = JobOption("Do dose-weighting?", true , "If set to Yes, the averaged micrographs will be dose-weighted.");
    joboptions["do_save_noDW"] = JobOption("Save non-dose weighted as well?", false, "Aligned but non-dose weighted images are sometimes useful in CTF estimation, although there is no difference in most cases. Whichever the choice, CTF refinement job is always done on dose-weighted particles.");
    joboptions["dose_per_frame"] = JobOption("Dose per frame (e/A2):", 1, 0, 5, 0.2, "Dose per movie frame (in electrons per square Angstrom).");
    joboptions["pre_exposure"] = JobOption("Pre-exposure (e/A2):", 0, 0, 5, 0.5, "Pre-exposure dose (in electrons per square Angstrom).");

    joboptions["do_save_ps"] = JobOption("Save sum of power spectra?", false, "Sum of non-dose weighted power spectra provides better signal for CTF estimation. The power spectra can be used by CTFFIND4 but not by GCTF. This option is not available for UCSF MotionCor2.");
    joboptions["group_for_ps"] = JobOption("Sum power spectra every e/A2:", 4, 0, 10, 0.5, "McMullan et al (Ultramicroscopy, 2015) sugggest summing power spectra every 4.0 e/A2 gives optimal Thon rings");
}

string RelionJob::getCommandsMotioncorrJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::MOTIONCORR_NAME, job_counter);

    string command = joboptions["nr_mpi"].getNumber() > 1 ?
        "`which relion_run_motioncorr_mpi`" : "`which relion_run_motioncorr`";

    // I/O
    if (joboptions["input_star_mics"].getString().empty())
    throw errorMsg("empty field for input STAR file...");

    command += " --i " + joboptions["input_star_mics"].getString();
    inputNodes.emplace_back(joboptions["input_star_mics"].getString(), joboptions["input_star_mics"].node_type);

    command += " --o " + outputname;
    outputName = outputname;
    outputNodes.emplace_back(outputname + "corrected_micrographs.star", Node::MICS);
    outputNodes.emplace_back(outputname + "logfile.pdf", Node::PDF_LOGFILE);

    command += " --first_frame_sum " + joboptions["first_frame_sum"].getString();
    command += " --last_frame_sum " + joboptions["last_frame_sum"].getString();

    if (joboptions["do_own_motioncor"].getBoolean()) {
        command += " --use_own ";
        command += " --j " + joboptions["nr_threads"].getString();
    } else {
        command += " --use_motioncor2 ";
        command += " --motioncor2_exe " + joboptions["fn_motioncor2_exe"].getString();

        if ((joboptions["other_motioncor2_args"].getString()).length() > 0)
            command += " --other_motioncor2_args \" " + joboptions["other_motioncor2_args"].getString() + " \"";

        // Which GPUs to use?
        command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";
    }

    const string fn_defect = joboptions["fn_defect"].getString();
    if (fn_defect.length() > 0)
        command += " --defect_file " + fn_defect;

    command += " --bin_factor "     + joboptions["bin_factor"].getString();
    command += " --bfactor "        + joboptions["bfactor"].getString();
    command += " --dose_per_frame " + joboptions["dose_per_frame"].getString();
    command += " --preexposure "    + joboptions["pre_exposure"].getString();
    command += " --patch_x "        + joboptions["patch_x"].getString();
    command += " --patch_y "        + joboptions["patch_y"].getString();
    command += " --eer_grouping "   + joboptions["eer_grouping"].getString();

    if (joboptions["group_frames"].getNumber() > 1.0)
        command += " --group_frames " + joboptions["group_frames"].getString();

    if (joboptions["fn_gain_ref"].getString().length() > 0) {

        int gain_rot = -1, gain_flip = -1;
        for (int i = 0; i <= 3; i++) {
            if (strcmp(joboptions["gain_rot"].getString().c_str(), job_gain_rotation_options[i].c_str()) == 0) {
                gain_rot = i;
                break;
            }
        }

        for (int i = 0; i <= 2; i++) {
            if (strcmp((joboptions["gain_flip"].getString()).c_str(), job_gain_flip_options[i].c_str()) == 0) {
                gain_flip = i;
                break;
            }
        }

        if (gain_rot == -1 || gain_flip == -1)
            REPORT_ERROR("Illegal gain_rot and/or gain_flip.");

        command += " --gainref " + joboptions["fn_gain_ref"].getString();
        command += " --gain_rot " + integerToString(gain_rot);
        command += " --gain_flip " + integerToString(gain_flip);
    }

    if (joboptions["do_dose_weighting"].getBoolean()) {
        command += " --dose_weighting ";
        if (joboptions["do_save_noDW"].getBoolean()) {
            command += " --save_noDW ";
        }
    }

    if (joboptions["do_save_ps"].getBoolean()) {
        if (!joboptions["do_own_motioncor"].getBoolean())
        throw "'Save sum of power spectra' is not available with UCSF MotionCor2.";

        float dose_for_ps = joboptions["group_for_ps"].getNumber();
        float dose_rate = joboptions["dose_per_frame"].getNumber();
        if (dose_rate <= 0)
        throw "Please specify the dose rate to calculate the grouping for power spectra.";

        if (dose_for_ps <= 0)
        throw "Invalid dose for the grouping for power spectra.";

        int grouping_for_ps = round(dose_for_ps / dose_rate);
        if (grouping_for_ps == 0) { grouping_for_ps = 1; }

        command += " --grouping_for_ps " + integerToString(grouping_for_ps) + " ";

    }

    if (is_continue)
    command += " --only_do_unfinished ";

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseCtffindJob() {
    hidden_name = ".gui_ctffind";

    joboptions["input_star_mics"] = JobOption("Input micrographs STAR file:", Node::MICS, "", "STAR files (*.star)", "A STAR file with all micrographs to run CTFFIND or Gctf on");
    joboptions["use_noDW"] = JobOption("Use micrograph without dose-weighting?", false, "If set to Yes, the CTF estimation will be done using the micrograph without dose-weighting as in rlnMicrographNameNoDW (_noDW.mrc from MotionCor2). If set to No, the normal rlnMicrographName will be used.");

    joboptions["do_phaseshift"] = JobOption("Estimate phase shifts?", false, "If set to Yes, CTFFIND4 will estimate the phase shift, e.g. as introduced by a Volta phase-plate");
    joboptions["phase_min"] = JobOption("Phase shift (deg) - Min:", string("0"), "Minimum, maximum and step size (in degrees) for the search of the phase shift");
    joboptions["phase_max"] = JobOption("Phase shift (deg) - Max:", string("180"), "Minimum, maximum and step size (in degrees) for the search of the phase shift");
    joboptions["phase_step"] = JobOption("Phase shift (deg) - Step:", string("10"), "Minimum, maximum and step size (in degrees) for the search of the phase shift");

    joboptions["dast"] = JobOption("Amount of astigmatism (A):", 100, 0, 2000, 100, "CTFFIND's dAst parameter, GCTF's -astm parameter");

    // CTFFIND options

    joboptions["use_ctffind4"] = JobOption("Use CTFFIND-4.1?", false, "If set to Yes, the wrapper will use CTFFIND4 (version 4.1) for CTF estimation. This includes thread-support, calculation of Thon rings from movie frames and phase-shift estimation for phase-plate data.");
    joboptions["use_given_ps"] = JobOption("Use power spectra from MotionCorr job?", false, "If set to Yes, the CTF estimation will be done using power spectra calculated during motion correction.");
    // Check for environment variable RELION_CTFFIND_EXECUTABLE
    const char *ctffind_exe = getenv("RELION_CTFFIND_EXECUTABLE");
    if (!ctffind_exe) { ctffind_exe = DEFAULT::CTFFINDLOCATION; }
    joboptions["fn_ctffind_exe"] = JobOption("CTFFIND-4.1 executable:", string(ctffind_exe), "*", ".", "Location of the CTFFIND (release 4.1 or later) executable. You can control the default of this field by setting environment variable RELION_CTFFIND_EXECUTABLE, or by editing the first few lines in src/gui_jobwindow.h and recompile the code.");
    joboptions["slow_search"] = JobOption("Use exhaustive search?", false, "If set to Yes, CTFFIND4 will use slower but more exhaustive search. This option is recommended for CTFFIND version 4.1.8 and earlier, but probably not necessary for 4.1.10 and later. It is also worth trying this option when astigmatism and/or phase shifts are difficult to fit.");

    joboptions["box"] = JobOption("FFT box size (pix):", 512, 64, 1024, 8, "CTFFIND's Box parameter");
    joboptions["resmin"] = JobOption("Minimum resolution (A):", 30, 10, 200, 10, "CTFFIND's ResMin parameter");
    joboptions["resmax"] = JobOption("Maximum resolution (A):", 5, 1, 20, 1, "CTFFIND's ResMax parameter");
    joboptions["dfmin"] = JobOption("Minimum defocus value (A):", 5000, 0, 25000, 1000, "CTFFIND's dFMin parameter");
    joboptions["dfmax"] = JobOption("Maximum defocus value (A):", 50000, 20000, 100000, 1000, "CTFFIND's dFMax parameter");
    joboptions["dfstep"] = JobOption("Defocus step size (A):", 500, 200, 2000, 100, "CTFFIND's FStep parameter");

    joboptions["ctf_win"] = JobOption("Estimate CTF on window size (pix) ", -1, -16, 4096, 16, "If a positive value is given, a squared window of this size at the center of the micrograph will be used to estimate the CTF. This may be useful to exclude parts of the micrograph that are unsuitable for CTF estimation, e.g. the labels at the edge of phtographic film. \n \n The original micrograph will be used (i.e. this option will be ignored) if a negative value is given.");

    joboptions["use_gctf"] = JobOption("Use Gctf instead?", false, "If set to Yes, Kai Zhang's Gctf program (which runs on NVIDIA GPUs) will be used instead of Niko Grigorieff's CTFFIND4.");
    // Check for environment variable RELION_GCTF_EXECUTABLE
    const char *gctf_exe = getenv("RELION_GCTF_EXECUTABLE");
    if (!gctf_exe) { gctf_exe = DEFAULT::GCTFLOCATION; }
    joboptions["fn_gctf_exe"] = JobOption("Gctf executable:", string(gctf_exe), "*", ".", "Location of the Gctf executable. You can control the default of this field by setting environment variable RELION_GCTF_EXECUTABLE, or by editing the first few lines in src/gui_jobwindow.h and recompile the code.");
    joboptions["do_ignore_ctffind_params"] = JobOption("Ignore 'Searches' parameters?", true, "If set to Yes, all parameters EXCEPT for phase shift search and its ranges on the 'Searches' tab will be ignored, and Gctf's default parameters will be used (box.size=1024; min.resol=50; max.resol=4; min.defocus=500; max.defocus=90000; step.defocus=500; astigm=1000) \n \
\nIf set to No, all parameters on the CTFFIND tab will be passed to Gctf.");
    joboptions["do_EPA"] = JobOption("Perform equi-phase averaging?", false, "If set to Yes, equi-phase averaging is used in the defocus refinement, otherwise basic rotational averaging will be performed.");
    joboptions["other_gctf_args"] = JobOption("Other Gctf options:", string(""), "Provide additional gctf options here.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','. ");
}

string RelionJob::getCommandsCtffindJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::CTFFIND_NAME, job_counter);

    const FileName fn_outstar = outputname + "micrographs_ctf.star";
    outputNodes.emplace_back(fn_outstar, Node::MICS);
    outputName = outputname;

    // PDF with histograms of the eigenvalues
    outputNodes.emplace_back(outputname + "logfile.pdf", Node::PDF_LOGFILE);

    if (joboptions["input_star_mics"].getString().empty())
    throw errorMsg("empty field for input STAR file...");

    inputNodes.emplace_back(joboptions["input_star_mics"].getString(), joboptions["input_star_mics"].node_type);

    string command = joboptions["nr_mpi"].getNumber() > 1 ?
        "`which relion_run_ctffind_mpi`" : "`which relion_run_ctffind`";

    command += " --i " + joboptions["input_star_mics"].getString();
    command += " --o " + outputname;
    command += " --Box " + joboptions["box"].getString();
    command += " --ResMin " + joboptions["resmin"].getString();
    command += " --ResMax " + joboptions["resmax"].getString();
    command += " --dFMin " + joboptions["dfmin"].getString();
    command += " --dFMax " + joboptions["dfmax"].getString();
    command += " --FStep " + joboptions["dfstep"].getString();
    command += " --dAst " + joboptions["dast"].getString();

    if (joboptions["use_noDW"].getBoolean())
        command += " --use_noDW ";

    if (joboptions["do_phaseshift"].getBoolean()) {
        command += " --do_phaseshift ";
        command += " --phase_min " + joboptions["phase_min"].getString();
        command += " --phase_max " + joboptions["phase_max"].getString();
        command += " --phase_step " + joboptions["phase_step"].getString();
    }

    if (joboptions["use_gctf"].getBoolean()) {
        command += " --use_gctf --gctf_exe " + joboptions["fn_gctf_exe"].getString();
        if (joboptions["do_ignore_ctffind_params"].getBoolean())
            command += " --ignore_ctffind_params";
        if (joboptions["do_EPA"].getBoolean())
            command += " --EPA";

        // GPU-allocation
        command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";

        if (
            joboptions["other_gctf_args"].getString().find("--phase_shift_H") != string::npos ||
            joboptions["other_gctf_args"].getString().find("--phase_shift_L") != string::npos ||
            joboptions["other_gctf_args"].getString().find("--phase_shift_S") != string::npos
        )
        throw "Please don't specify --phase_shift_L, H, S in 'Other Gctf options'. Use 'Estimate phase shifts' and 'Phase shift - Min, Max, Step' instead.";

        if (!joboptions["other_gctf_args"].getString().empty())
            command += " --extra_gctf_options \" " + joboptions["other_gctf_args"].getString() + " \"";

    } else if (joboptions["use_ctffind4"].getBoolean()) {
        command += " --ctffind_exe " + joboptions["fn_ctffind_exe"].getString();
        command += " --ctfWin " + joboptions["ctf_win"].getString();
        command += " --is_ctffind4 ";
        if (!joboptions["slow_search"].getBoolean()) {
            command += " --fast_search ";
        }
        if (joboptions["use_given_ps"].getBoolean()) {
            command += " --use_given_ps ";
        }
    } else {
        throw errorMsg("Please select use of CTFFIND4.1 or Gctf...");
    }

    if (is_continue) { command += " --only_do_unfinished "; }

    // Other arguments
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseManualpickJob() {
    hidden_name = ".gui_manualpick";

    joboptions["fn_in"] = JobOption("Input micrographs:", Node::MICS, "", "Input micrographs (*.{star,mrc})", "Input STAR file (with or without CTF information), OR a unix-type wildcard with all micrographs in MRC format (in this case no CTFs can be used).");

    joboptions["diameter"] = JobOption("Particle diameter (A):", 100, 0, 500, 50, "The diameter of the circle used around picked particles (in Angstroms). Only used for display." );
    joboptions["micscale"] = JobOption("Scale for micrographs:", 0.2, 0.1, 1, 0.05, "The micrographs will be displayed at this relative scale, i.e. a value of 0.5 means that only every second pixel will be displayed." );
    joboptions["sigma_contrast"] = JobOption("Sigma contrast:", 3, 0, 10, 0.5, "The micrographs will be displayed with the black value set to the average of all values MINUS this values times the standard deviation of all values in the micrograph, and the white value will be set \
to the average PLUS this value times the standard deviation. Use zero to set the minimum value in the micrograph to black, and the maximum value to white ");
    joboptions["white_val"] = JobOption("White value:", 0, 0, 512, 16, "Use non-zero values to set the value of the whitest pixel in the micrograph.");
    joboptions["black_val"] = JobOption("Black value:", 0, 0, 512, 16, "Use non-zero values to set the value of the blackest pixel in the micrograph.");

    joboptions["lowpass"] = JobOption("Lowpass filter (A)", 20, 10, 100, 5, "Lowpass filter that will be applied to the micrographs. Give a negative value to skip the lowpass filter.");
    joboptions["highpass"] = JobOption("Highpass filter (A)", -1, 100, 1000, 100, "Highpass filter that will be applied to the micrographs. This may be useful to get rid of background ramps due to uneven ice distributions. Give a negative value to skip the highpass filter. Useful values are often in the range of 200-400 Angstroms.");
    joboptions["angpix"] = JobOption("Pixel size (A)", -1, 0.3, 5, 0.1, "Pixel size in Angstroms. This will be used to calculate the filters and the particle diameter in pixels. If a CTF-containing STAR file is input, then the value given here will be ignored, and the pixel size will be calculated from the values in the STAR file. A negative value can then be given here.");

    joboptions["do_startend"] = JobOption("Pick start-end coordinates helices?", false, "If set to true, start and end coordinates are picked subsequently and a line will be drawn between each pair");

    joboptions["ctfscale"] = JobOption("Scale for CTF image:", 1, 0.1, 2, 0.1, "CTFFINDs CTF image (with the Thonrings) will be displayed at this relative scale, i.e. a value of 0.5 means that only every second pixel will be displayed." );

    joboptions["do_color"] = JobOption("Blue<>red color particles?", false, "If set to true, then the circles for each particles are coloured from red to blue (or the other way around) for a given metadatalabel. If this metadatalabel is not in the picked coordinates STAR file \
(basically only the rlnAutopickFigureOfMerit or rlnClassNumber) would be useful values there, then you may provide an additional STAR file (e.g. after classification/refinement below. Particles with values -999, or that are not in the additional STAR file will be coloured the default color: green");
    joboptions["color_label"] = JobOption("MetaDataLabel for color:", string("rlnParticleSelectZScore"), "The Metadata label of the value to plot from red<>blue. Useful examples might be: \n \
rlnParticleSelectZScore \n rlnClassNumber \n rlnAutopickFigureOfMerit \n rlnAngleTilt \n rlnLogLikeliContribution \n rlnMaxValueProbDistribution \n rlnNrOfSignificantSamples\n");
    joboptions["fn_color"] = JobOption("STAR file with color label: ", "", "STAR file (*.star)", ".", "The program will figure out which particles in this STAR file are on the current micrograph and color their circles according to the value in the corresponding column. \
Particles that are not in this STAR file, but present in the picked coordinates file will be colored green. If this field is left empty, then the color label (e.g. rlnAutopickFigureOfMerit) should be present in the coordinates STAR file.");
    joboptions["blue_value"] = JobOption("Blue value: ", 0.0, 0.0, 4.0, 0.1, "The value of this entry will be blue. There will be a linear scale from blue to red, according to this value and the one given below.");
    joboptions["red_value"] = JobOption("Red value: ", 2.0, 0.0, 4.0, 0.1, "The value of this entry will be red. There will be a linear scale from blue to red, according to this value and the one given above.");
}

string RelionJob::getCommandsManualpickJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::MANUALPICK_NAME, job_counter);
    string command = "`which relion_manualpick`";

    if (joboptions["fn_in"].getString().empty())
    throw errorMsg("empty field for input STAR file...");

    command += " --i " + joboptions["fn_in"].getString();
    inputNodes.emplace_back(joboptions["fn_in"].getString(), joboptions["fn_in"].node_type);

    command += " --odir " + outputname;
    command += " --pickname manualpick";

    const FileName fn_suffix = outputname + "coords_suffix_manualpick.star";
    outputNodes.emplace_back(fn_suffix, Node::MIC_COORDS);

    // Allow saving, and always save default selection file upon launching the program
    const FileName fn_outstar = outputname + "micrographs_selected.star";
    outputNodes.emplace_back(fn_outstar, Node::MICS);
    command += " --allow_save   --fast_save --selection " + fn_outstar;

    command += " --scale " + joboptions["micscale"].getString();
    command += " --sigma_contrast " + joboptions["sigma_contrast"].getString();
    command += " --black " + joboptions["black_val"].getString();
    command += " --white " + joboptions["white_val"].getString();

    if (joboptions["lowpass"].getNumber() > 0.0)
    command += " --lowpass " + joboptions["lowpass"].getString();
    if (joboptions["highpass"].getNumber() > 0.0)
    command += " --highpass " + joboptions["highpass"].getString();
    if (joboptions["angpix"].getNumber() > 0.0)
    command += " --angpix " + joboptions["angpix"].getString();

    command += " --ctf_scale " + joboptions["ctfscale"].getString();

    command += " --particle_diameter " + joboptions["diameter"].getString();

    if (joboptions["do_startend"].getBoolean())
        command += " --pick_start_end ";

    if (joboptions["do_color"].getBoolean()) {
        command += " --color_label " + joboptions["color_label"].getString();
        command += " --blue " + joboptions["blue_value"].getString();
        command += " --red " + joboptions["red_value"].getString();
        if (joboptions["fn_color"].getString().length() > 0)
            command += " --color_star " + joboptions["fn_color"].getString();
    }

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    // Also make the suffix file (do this after previous command was pushed back!)
    // Inside it, store the name of the micrograph STAR file, so we can display these later
    FileName fn_pre, fn_jobnr, fn_post;
    decomposePipelineSymlinkName(joboptions["fn_in"].getString(), fn_pre, fn_jobnr, fn_post);
    command = "echo " + fn_pre + fn_jobnr + fn_post + " > " + fn_suffix;
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseAutopickJob() {
    hidden_name = ".gui_autopick";

    joboptions["fn_input_autopick"] = JobOption(
        "Input micrographs for autopick:", Node::MICS, "",
        "Input micrographs (*.{star})",
        "Input STAR file (preferably with CTF information) "
        "with all micrographs to pick from."
    );
    joboptions["angpix"] = JobOption(
        "Pixel size in micrographs (A)", -1, 0.3, 5, 0.1,
        "Pixel size in Angstroms. "
        "If a CTF-containing STAR file is input, "
        "then the value given here will be ignored, "
        "and the pixel size will be calculated from the values in the STAR file. "
        "A negative value can then be given here."
    );

    joboptions["do_log"] = JobOption(
        "OR: use Laplacian-of-Gaussian?", false,
        "If set to Yes, a Laplacian-of-Gaussian blob detection will be used "
        "(you can then leave the 'References' field empty). "
        "The preferred way to autopick is by setting this to No "
        "and providing references that were generated by 2D classification from this data set in RELION. "
        "The Laplacian-of-Gaussian method may be useful to kickstart a new data set. "
        "Please note that some options in the autopick tab are ignored in this method. "
        "For details, see each option's help message."
    );
    joboptions["log_diam_min"] = JobOption(
        "Min. diameter for LoG filter (A)", 200, 50, 500, 10,
        "The smallest allowed diameter for the blob-detection algorithm. "
        "This should correspond to the smallest size of your particles in Angstroms."
    );
    joboptions["log_diam_max"] = JobOption(
        "Max. diameter for LoG filter (A)", 250, 50, 500, 10,
        "The largest allowed diameter for the blob-detection algorithm. "
        "This should correspond to the largest size of your particles in Angstroms."
    );
    joboptions["log_invert"] = JobOption(
        "Are the particles white?", false,
        "Set this option to No if the particles are black, "
        "and to Yes if the particles are white."
    );
    joboptions["log_maxres"] = JobOption(
        "Maximum resolution to consider (A)", 20, 10, 100, 5,
        "The Laplacian-of-Gaussian filter will be applied to downscaled micrographs with the corresponding size. "
        "Give a negative value to skip downscaling."
    );
    joboptions["log_adjust_thr"] = JobOption(
        "Adjust default threshold (stddev):", 0, -1.0, 1.0, 0.05,
        "Use this to pick more (negative number -> lower threshold) or fewer (positive number -> higher threshold) particles compared to the default setting. "
        "The threshold is moved this many standard deviations away from the average."
    );
    joboptions["log_upper_thr"] = JobOption(
        "Upper threshold (stddev):", 999.0, 0.0, 10.0, 0.5,
        "Use this to discard picks with LoG thresholds that are this many standard deviations above the average, "
        "e.g. to avoid high contrast contamination like ice and ethane droplets. "
        "Good values depend on the contrast of micrographs and need to be determined by trial and error; "
        "for low contrast micrographs, values of ~ 1.5 may be reasonable, "
        "but the same value will be too low for high-contrast micrographs."
    );

    joboptions["fn_refs_autopick"] = JobOption(
        "2D references:", Node::REFS2D, "", "Input references (*.{star,mrcs})",
        "Input STAR file or MRC stack with the 2D references to be used for picking. "
        "Note that the absolute greyscale needs to be correct, "
        "so only use images created by RELION itself, e.g. by 2D class averaging or projecting a RELION reconstruction."
    );
    joboptions["do_ref3d"]= JobOption("OR: provide a 3D reference?", false, "Set this option to Yes if you want to provide a 3D map, which will be projected into multiple directions to generate 2D references.");
    joboptions["fn_ref3d_autopick"] = JobOption("3D reference:", Node::REF3D, "", "Input reference (*.{mrc})", "Input MRC file with the 3D reference maps, from which 2D references will be made by projection. Note that the absolute greyscale needs to be correct, so only use maps created by RELION itself from this data set.");
    joboptions["ref3d_symmetry"] = JobOption("Symmetry:", string("C1"), "Symmetry point group of the 3D reference. Only projections in the asymmetric part of the sphere will be generated.");
    joboptions["ref3d_sampling"] = JobOption("3D angular sampling:", job_sampling_options, 0, "There are only a few discrete \
angular samplings possible because we use the HealPix library to generate the sampling of the first two Euler angles on the sphere. \
The samplings are approximate numbers and vary slightly over the sphere.\n\n For autopicking, 30 degrees is usually fine enough, but for highly symmetrical objects one may need to go finer to adequately sample the asymmetric part of the sphere.");

    joboptions["particle_diameter"] = JobOption("Mask diameter (A)", -1, 0, 2000, 20, "Diameter of the circular mask that will be applied around the templates in Angstroms. When set to a negative value, this value is estimated automatically from the templates themselves.");
    joboptions["lowpass"] = JobOption("Lowpass filter references (A)", 20, 10, 100, 5, "Lowpass filter that will be applied to the references before template matching. Do NOT use very high-resolution templates to search your micrographs. The signal will be too weak at high resolution anyway, and you may find Einstein from noise.... Give a negative value to skip the lowpass filter.");
    joboptions["highpass"] = JobOption("Highpass filter (A)", -1, 100, 1000, 100, "Highpass filter that will be applied to the micrographs. This may be useful to get rid of background ramps due to uneven ice distributions. Give a negative value to skip the highpass filter.  Useful values are often in the range of 200-400 Angstroms.");
    joboptions["angpix_ref"] = JobOption("Pixel size in references (A)", -1, 0.3, 5, 0.1, "Pixel size in Angstroms for the provided reference images. This will be used to calculate the filters and the particle diameter in pixels. If a negative value is given here, the pixel size in the references will be assumed to be the same as the one in the micrographs, i.e. the particles that were used to make the references were not rescaled upon extraction.");
    joboptions["psi_sampling_autopick"] = JobOption("In-plane angular sampling (deg)", 5, 1, 30, 1, "Angular sampling in degrees for exhaustive searches of the in-plane rotations for all references.");

    joboptions["do_invert_refs"] = JobOption("References have inverted contrast?", true, "Set to Yes to indicate that the reference have inverted contrast with respect to the particles in the micrographs.");
    joboptions["do_ctf_autopick"] = JobOption("Are References CTF corrected?", true, "Set to Yes if the references were created with CTF-correction inside RELION. \n \n If set to Yes, the input micrographs can only be given as a STAR file, which should contain the CTF information for each micrograph.");
    joboptions["do_ignore_first_ctfpeak_autopick"] = JobOption("Ignore CTFs until first peak?", false, "Set this to Yes, only if this option was also used to generate the references.");

    joboptions["threshold_autopick"] = JobOption("Picking threshold:", 0.05, 0, 1.0, 0.01, "Use lower thresholds to pick more particles (and more junk probably).\
\n\nThis option is ignored in the Laplacian-of-Gaussian picker. Please use 'Adjust default threshold' in the 'Laplacian' tab instead.");
    joboptions["mindist_autopick"] = JobOption("Minimum inter-particle distance (A):", 100, 0, 1000, 20, "Particles closer together than this distance will be consider to be a single cluster. From each cluster, only one particle will be picked. \
\n\nThis option takes no effect for picking helical segments. The inter-box distance is calculated with the number of asymmetrical units and the helical rise on 'Helix' tab. This option is also ignored in the Laplacian-of-Gaussian picker. The inter-box distance is calculated from particle diameters.");
    joboptions["maxstddevnoise_autopick"] = JobOption("Maximum stddev noise:", 1.1, 0.9, 1.5, 0.02, "This is useful to prevent picking in carbon areas, or areas with big contamination features. Peaks in areas where the background standard deviation in the normalized micrographs is higher than this value will be ignored. Useful values are probably in the range 1.0 to 1.2. Set to -1 to switch off the feature to eliminate peaks due to high background standard deviations.\
\n\nThis option is ignored in the Laplacian-of-Gaussian picker.");
    joboptions["minavgnoise_autopick"] = JobOption("Minimum avg noise:", -999.0, -2, 0.5, 0.05, "This is useful to prevent picking in carbon areas, or areas with big contamination features. Peaks in areas where the background standard deviation in the normalized micrographs is higher than this value will be ignored. Useful values are probably in the range -0.5 to 0. Set to -999 to switch off the feature to eliminate peaks due to low average background densities.\
\n\nThis option is ignored in the Laplacian-of-Gaussian picker.");
    joboptions["do_write_fom_maps"] = JobOption("Write FOM maps?", false, "If set to Yes, intermediate probability maps will be written out, which (upon reading them back in) will speed up tremendously the optimization of the threshold and inter-particle distance parameters. However, with this option, one cannot run in parallel, as disc I/O is very heavy with this option set.");
    joboptions["do_read_fom_maps"] = JobOption("Read FOM maps?", false, "If written out previously, read the FOM maps back in and re-run the picking to quickly find the optimal threshold and inter-particle distance parameters");

    joboptions["shrink"] = JobOption("Shrink factor:", 0, 0, 1, 0.1, "This is useful to speed up the calculations, and to make them less memory-intensive. The micrographs will be downscaled (shrunk) to calculate the cross-correlations, and peak searching will be done in the downscaled FOM maps. When set to 0, the micrographs will de downscaled to the lowpass filter of the references, a value between 0 and 1 will downscale the micrographs by that factor. Note that the results will not be exactly the same when you shrink micrographs!\
\n\nIn the Laplacian-of-Gaussian picker, this option is ignored and the shrink factor always becomes 0.");
    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration. The Laplacian-of-Gaussian picker does not support GPU.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':'. For example: 0:1:0:1:0:1");

    joboptions["do_pick_helical_segments"] = JobOption("Pick 2D helical segments?", false, "Set to Yes if you want to pick 2D helical segments.");
    joboptions["do_amyloid"] = JobOption("Pick amyloid segments?", false, "Set to Yes if you want to use the algorithm that was developed specifically for picking amyloids.");

    joboptions["helical_tube_outer_diameter"] = JobOption("Tube diameter (A): ", 200, 100, 1000, 10, "Outer diameter (in Angstroms) of helical tubes. \
This value should be slightly larger than the actual width of the tubes.");
    joboptions["helical_nr_asu"] = JobOption("Number of unique asymmetrical units:", 1, 1, 100, 1, "Number of unique helical asymmetrical units in each segment box. This integer should not be less than 1. The inter-box distance (pixels) = helical rise (Angstroms) * number of asymmetrical units / pixel size (Angstroms). \
The optimal inter-box distance might also depend on the box size, the helical rise and the flexibility of the structure. In general, an inter-box distance of ~10% * the box size seems appropriate.");
    joboptions["helical_rise"] = JobOption("Helical rise (A):", -1, 0, 100, 0.01, "Helical rise in Angstroms. (Please click '?' next to the option above for details about how the inter-box distance is calculated.)");
    joboptions["helical_tube_kappa_max"] = JobOption("Maximum curvature (kappa): ", 0.1, 0.05, 0.5, 0.01, "Maximum curvature allowed for picking helical tubes. \
Kappa = 0.3 means that the curvature of the picked helical tubes should not be larger than 30% the curvature of a circle (diameter = particle mask diameter). \
Kappa ~ 0.05 is recommended for long and straight tubes (e.g. TMV, VipA/VipB and AChR tubes) while 0.20 ~ 0.40 seems suitable for flexible ones (e.g. ParM and MAVS-CARD filaments).");
    joboptions["helical_tube_length_min"] = JobOption("Minimum length (A): ", -1, 100, 1000, 10, "Minimum length (in Angstroms) of helical tubes for auto-picking. \
Helical tubes with shorter lengths will not be picked. Note that a long helical tube seen by human eye might be treated as short broken pieces due to low FOM values or high picking threshold.");
}

string RelionJob::getCommandsAutopickJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::AUTOPICK_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
        "`which relion_autopick_mpi`" : "`which relion_autopick`";

    // Input
    if (joboptions["fn_input_autopick"].getString().empty())
    throw errorMsg("empty field for input STAR file...");
    command += " --i " + joboptions["fn_input_autopick"].getString();
    inputNodes.emplace_back(joboptions["fn_input_autopick"].getString(), joboptions["fn_input_autopick"].node_type);

    // Output
    outputNodes.emplace_back(outputname + "coords_suffix_autopick.star", Node::MIC_COORDS);

    // PDF with histograms of the eigenvalues
    outputNodes.emplace_back(outputname + "logfile.pdf", Node::PDF_LOGFILE);

    command += " --odir " + outputname;
    command += " --pickname autopick";

    if (joboptions["do_log"].getBoolean()) {
        if (joboptions["use_gpu"].getBoolean())
        throw errorMsg("The Laplacian-of-Gaussian picker does not support GPU.");

        command += " --LoG ";
        command += " --LoG_diam_min " + joboptions["log_diam_min"].getString();
        command += " --LoG_diam_max " + joboptions["log_diam_max"].getString();
        command += " --shrink 0 --lowpass " + joboptions["log_maxres"].getString();
        command += " --LoG_adjust_threshold " + joboptions["log_adjust_thr"].getString();

        if (joboptions["log_upper_thr"].getNumber() < 999.0)  // May throw
        command += " --LoG_upper_threshold " + joboptions["log_upper_thr"].getString();

        if (joboptions["log_invert"].getBoolean())
            command += " --Log_invert ";
    } else {

        if (joboptions["do_ref3d"].getBoolean()) {

            if (joboptions["fn_ref3d_autopick"].getString().empty())
            throw errorMsg("empty field for 3D reference...");

            command += " --ref " + joboptions["fn_ref3d_autopick"].getString();
            inputNodes.emplace_back(joboptions["fn_ref3d_autopick"].getString(), Node::REF3D);
            command += " --sym " + joboptions["ref3d_symmetry"].getString();

            // Sampling
            int ref3d_sampling = JobOption::getHealPixOrder(joboptions["ref3d_sampling"].getString());
            if (ref3d_sampling <= 0)
            throw "Wrong choice for ref3d_sampling";

            command += " --healpix_order " + integerToString(ref3d_sampling);

        } else {

            if (joboptions["fn_refs_autopick"].getString().empty())
            throw errorMsg("empty field for references...");

            command += " --ref " + joboptions["fn_refs_autopick"].getString();
            inputNodes.emplace_back(joboptions["fn_refs_autopick"].getString(), Node::REFS2D);

        }

        if (joboptions["do_invert_refs"].getBoolean())
            command += " --invert ";

        if (joboptions["do_ctf_autopick"].getBoolean()) {
            command += " --ctf ";
            if (joboptions["do_ignore_first_ctfpeak_autopick"].getBoolean())
                command += " --ctf_intact_first_peak ";
        }
        command += " --ang " + joboptions["psi_sampling_autopick"].getString();

        command += " --shrink " + joboptions["shrink"].getString();

        // Any of these JobOption::getNumber() calls may throw

        if (joboptions["lowpass"].getNumber() > 0.0)
        command += " --lowpass " + joboptions["lowpass"].getString();
        if (joboptions["highpass"].getNumber() > 0.0)
        command += " --highpass " + joboptions["highpass"].getString();
        if (joboptions["angpix"].getNumber() > 0.0)
        command += " --angpix " + joboptions["angpix"].getString();
        if (joboptions["angpix_ref"].getNumber() > 0.0)
        command += " --angpix_ref " + joboptions["angpix_ref"].getString();
        if (joboptions["particle_diameter"].getNumber() > 0.0)
        command += " --particle_diameter " + joboptions["particle_diameter"].getString();

        command += " --threshold " + joboptions["threshold_autopick"].getString();

        if (joboptions["do_pick_helical_segments"].getBoolean()) {
            command += " --min_distance " + floatToString(joboptions["helical_nr_asu"].getNumber() * joboptions["helical_rise"].getNumber());
        } else {
            command += " --min_distance " + joboptions["mindist_autopick"].getString();
        }

        command += " --max_stddev_noise " + joboptions["maxstddevnoise_autopick"].getString();

        if (joboptions["minavgnoise_autopick"].getNumber() > -900.0)
        command += " --min_avg_noise " + joboptions["minavgnoise_autopick"].getString();

        // Helix
        if (joboptions["do_pick_helical_segments"].getBoolean()) {
            command += " --helix";
            if (joboptions["do_amyloid"].getBoolean())
                command += " --amyloid";
            command += " --helical_tube_outer_diameter " + joboptions["helical_tube_outer_diameter"].getString();
            command += " --helical_tube_kappa_max " + joboptions["helical_tube_kappa_max"].getString();
            command += " --helical_tube_length_min " + joboptions["helical_tube_length_min"].getString();
        }

        // GPU stuff
        if (joboptions["use_gpu"].getBoolean()) {
            // for the moment always use --shrink 0 with GPUs ...
            command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";
        }

    }

    // Although mainly for debugging, LoG-picking does have write/read_fom_maps...
    if (joboptions["do_write_fom_maps"].getBoolean())
        command += " --write_fom_maps ";

    if (joboptions["do_read_fom_maps"].getBoolean())
        command += " --read_fom_maps ";

    if (is_continue && !joboptions["do_read_fom_maps"].getBoolean() && !joboptions["do_write_fom_maps"].getBoolean())
        command += " --only_do_unfinished ";


    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    // Also touch the suffix file. Do this after the first command had completed
    // Instead of the symlink from the alias, use the original jobnr filename
    FileName fn_pre, fn_jobnr, fn_post;
    decomposePipelineSymlinkName(joboptions["fn_input_autopick"].getString(), fn_pre, fn_jobnr, fn_post);
    command = "echo " + fn_pre + fn_jobnr + fn_post + " > " +  outputname + "coords_suffix_autopick.star";
    commands.push_back(command.c_str());

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseExtractJob() {
    hidden_name = ".gui_extract";

    joboptions["star_mics"]= JobOption("micrograph STAR file:", Node::MICS, "", "Input STAR file (*.{star})", "Filename of the STAR file that contains all micrographs from which to extract particles.");
    joboptions["coords_suffix"] = JobOption("Input coordinates:", Node::MIC_COORDS, "", "Input coords_suffix file ({coords_suffix}*)", "Filename of the coords_suffix file with the directory structure and the suffix of all coordinate files.");
    joboptions["do_reextract"] = JobOption("OR re-extract refined particles? ", false, "If set to Yes, the input Coordinates above will be ignored. Instead, one uses a _data.star file from a previous 2D or 3D refinement to re-extract the particles in that refinement, possibly re-centered with their refined origin offsets. This is particularly useful when going from binned to unbinned particles.");
    joboptions["fndata_reextract"] = JobOption("Refined particles STAR file: ", Node::PART_DATA, "", "Input STAR file (*.{star})", "Filename of the STAR file with the refined particle coordinates, e.g. from a previous 2D or 3D classification or auto-refine run.");
    joboptions["do_reset_offsets"] = JobOption("Reset the refined offsets to zero? ", false, "If set to Yes, the input origin offsets will be reset to zero. This may be useful after 2D classification of helical segments, where one does not want neighbouring segments to be translated on top of each other for a subsequent 3D refinement or classification.");
    joboptions["do_recenter"] = JobOption("OR: re-center refined coordinates? ", false, "If set to Yes, the input coordinates will be re-centered according to the refined origin offsets in the provided _data.star file. The unit is pixel, not angstrom. The origin is at the center of the box, not at the corner.");
    joboptions["recenter_x"] = JobOption("Re-center on X-coordinate (in pix): ", string("0"), "Re-extract particles centered on this X-coordinate (in pixels in the reference)");
    joboptions["recenter_y"] = JobOption("Re-center on Y-coordinate (in pix): ", string("0"), "Re-extract particles centered on this Y-coordinate (in pixels in the reference)");
    joboptions["recenter_z"] = JobOption("Re-center on Z-coordinate (in pix): ", string("0"), "Re-extract particles centered on this Z-coordinate (in pixels in the reference)");
    joboptions["extract_size"] = JobOption("Particle box size (pix):", 128, 64, 512, 8, "Size of the extracted particles (in pixels). This should be an even number!");
    joboptions["do_invert"] = JobOption("Invert contrast?", true, "If set to Yes, the contrast in the particles will be inverted.");

    joboptions["do_norm"] = JobOption("Normalize particles?", true, "If set to Yes, particles will be normalized in the way RELION prefers it.");
    joboptions["bg_diameter"] = JobOption("Diameter background circle (pix):", -1, -1, 600, 10, "Particles will be normalized to a mean value of zero and a standard-deviation of one for all pixels in the background area.\
The background area is defined as all pixels outside a circle with this given diameter in pixels (before rescaling). When specifying a negative value, a default value of 75% of the Particle box size will be used.");
    joboptions["white_dust"] = JobOption("Stddev for white dust removal: ", -1, -1, 10, 0.1, "Remove very white pixels from the extracted particles. \
Pixels values higher than this many times the image stddev will be replaced with values from a Gaussian distribution. \n \n Use negative value to switch off dust removal.");
    joboptions["black_dust"] = JobOption("Stddev for black dust removal: ", -1, -1, 10, 0.1, "Remove very black pixels from the extracted particles. \
Pixels values higher than this many times the image stddev will be replaced with values from a Gaussian distribution. \n \n Use negative value to switch off dust removal.");
    joboptions["do_rescale"] = JobOption("Rescale particles?", false, "If set to Yes, particles will be re-scaled. Note that the particle diameter below will be in the down-scaled images.");
    joboptions["rescale"] = JobOption("Re-scaled size (pixels): ", 128, 64, 512, 8, "The re-scaled value needs to be an even number");

    joboptions["do_extract_helix"] = JobOption("Extract helical segments?", false, "Set to Yes if you want to extract helical segments. RELION (.star), EMAN2 (.box) and XIMDISP (.coords) formats of tube or segment coordinates are supported.");
    joboptions["helical_tube_outer_diameter"] = JobOption("Tube diameter (A): ", 200, 100, 1000, 10, "Outer diameter (in Angstroms) of helical tubes. \
This value should be slightly larger than the actual width of helical tubes.");
    joboptions["helical_bimodal_angular_priors"] = JobOption("Use bimodal angular priors?", true, "Normally it should be set to Yes and bimodal angular priors will be applied in the following classification and refinement jobs. \
Set to No if the 3D helix looks the same when rotated upside down.");
    joboptions["do_extract_helical_tubes"] = JobOption("Coordinates are start-end only?", true, "Set to Yes if you want to extract helical segments from manually picked tube coordinates (starting and end points of helical tubes in RELION, EMAN or XIMDISP format). \
Set to No if segment coordinates (RELION auto-picked results or EMAN / XIMDISP segments) are provided.");
    joboptions["do_cut_into_segments"] = JobOption("Cut helical tubes into segments?", true, "Set to Yes if you want to extract multiple helical segments with a fixed inter-box distance. \
If it is set to No, only one box at the center of each helical tube will be extracted.");
    joboptions["helical_nr_asu"] = JobOption("Number of unique asymmetrical units:", 1, 1, 100, 1, "Number of unique helical asymmetrical units in each segment box. This integer should not be less than 1. The inter-box distance (pixels) = helical rise (Angstroms) * number of asymmetrical units / pixel size (Angstroms). \
The optimal inter-box distance might also depend on the box size, the helical rise and the flexibility of the structure. In general, an inter-box distance of ~10% * the box size seems appropriate.");
    joboptions["helical_rise"] = JobOption("Helical rise (A):", 1, 0, 100, 0.01, "Helical rise in Angstroms. (Please click '?' next to the option above for details about how the inter-box distance is calculated.)");

}

string RelionJob::getCommandsExtractJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::EXTRACT_NAME, job_counter);
    string command = "which relion_preprocess";
    if (joboptions["nr_mpi"].getNumber() > 1)  // May throw
    command += "_mpi";

    command = "`" + command + "`";

    // Input
    if (joboptions["star_mics"].getString().empty())
    throw errorMsg("empty field for input STAR file...");

    command += " --i " + joboptions["star_mics"].getString();
    inputNodes.emplace_back(joboptions["star_mics"].getString(), joboptions["star_mics"].node_type);

    if (joboptions["do_reextract"].getBoolean()) {

        if (joboptions["fndata_reextract"].getString().empty())
        throw errorMsg("empty field for refined particles STAR file...");

        if (joboptions["do_reset_offsets"].getBoolean() && joboptions["do_recenter"].getBoolean())
        throw errorMsg("you cannot both reset refined offsets and recenter on refined coordinates, choose one...");

        command += " --reextract_data_star " + joboptions["fndata_reextract"].getString();
        inputNodes.emplace_back(joboptions["fndata_reextract"].getString(), joboptions["fndata_reextract"].node_type);
        if (joboptions["do_reset_offsets"].getBoolean()) {
            command += " --reset_offsets";
        } else if (joboptions["do_recenter"].getBoolean()) {
            command += string(" --recenter")
                    + " --recenter_x " + joboptions["recenter_x"].getString()
                    + " --recenter_y " + joboptions["recenter_y"].getString()
                    + " --recenter_z " + joboptions["recenter_z"].getString();
        }
    } else {
        const FileName suffix = joboptions["coords_suffix"].getString();
        if (suffix.empty())
        throw errorMsg("empty field for coordinate STAR file...");

        command += " --coord_dir " + suffix.beforeLastOf("/") + "/";
        command += " --coord_suffix " + suffix.afterLastOf("/").without("coords_suffix");
        inputNodes.emplace_back(joboptions["coords_suffix"].getString(), joboptions["coords_suffix"].node_type);
    }

    // Output
    const FileName fn_ostar = outputname + "particles.star";
    outputNodes.emplace_back(fn_ostar, Node::PART_DATA);
    command += " --part_star " + fn_ostar;
    command += " --part_dir " + outputname;
    command += " --extract";
    command += " --extract_size " + joboptions["extract_size"].getString();

    // Operate stuff
    // Get an integer number for the bg_diameter
    // Any of these JobOption::getNumber() calls may throw
    RFLOAT bg_diameter = joboptions["bg_diameter"].getNumber() < 0.0 ?
        0.75 * joboptions["extract_size"].getNumber() :
                joboptions["bg_diameter"].getNumber();
    RFLOAT bg_radius = bg_diameter / 2.0;
    if (joboptions["do_rescale"].getBoolean()) {
        command += " --scale " + joboptions["rescale"].getString();
        bg_radius *= joboptions["rescale"].getNumber();
        bg_radius /= joboptions["extract_size"].getNumber();
    }
    if (joboptions["do_norm"].getBoolean()) {
        // Get an integer for bg_radius
        bg_radius = (int) bg_radius;
        command += " --norm --bg_radius " + floatToString(bg_radius)
                +  " --white_dust " + joboptions["white_dust"].getString()
                +  " --black_dust " + joboptions["black_dust"].getString();
    }

    if (joboptions["do_invert"].getBoolean()) {
        command += " --invert_contrast ";
    }

    // Helix
    if (joboptions["do_extract_helix"].getBoolean()) {
        command += " --helix";
        command += " --helical_outer_diameter " + joboptions["helical_tube_outer_diameter"].getString();
        if (joboptions["helical_bimodal_angular_priors"].getBoolean())
            command += " --helical_bimodal_angular_priors";
        if (joboptions["do_extract_helical_tubes"].getBoolean()) {
            command += " --helical_tubes";
            if (joboptions["do_cut_into_segments"].getBoolean()) {
                command += " --helical_cut_into_segments";
                command += " --helical_nr_asu " + joboptions["helical_nr_asu"].getString();
                command += " --helical_rise " + joboptions["helical_rise"].getString();
            } else {
                command += " --helical_nr_asu 1";
                command += "--helical_rise 1";
            }
        }
    }

    if (is_continue)
        command += " --only_do_unfinished ";

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    if (
        joboptions["do_reextract"].getBoolean() || (
        joboptions["do_extract_helix"].getBoolean() &&
        joboptions["do_extract_helical_tubes"].getBoolean())
    ) {
        // Also touch the suffix file. Do this after the first command had completed
        command = "echo " + joboptions["star_mics"].getString() + " > " +  outputname + "coords_suffix_extract.star";
        commands.push_back(command.c_str());

        outputNodes.emplace_back(outputname + "coords_suffix_extract.star", Node::MIC_COORDS);
    }

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseSelectJob() {
    hidden_name = ".gui_select";

    joboptions["fn_model"] = JobOption("Select classes from model.star:", Node::MODEL, "", "STAR files (*.star)", "A _model.star file from a previous 2D or 3D classification run to select classes from.");
    joboptions["fn_mic"] = JobOption("OR select from micrographs.star:", Node::MICS, "", "STAR files (*.star)", "A micrographs.star file to select micrographs from.");
    joboptions["fn_data"] = JobOption("OR select from particles.star:", Node::PART_DATA, "", "STAR files (*.star)", "A particles.star file to select individual particles from.");
    joboptions["fn_coords"] = JobOption("OR select from picked coords:", Node::MIC_COORDS, "", "STAR files (coords_suffix*.star)", "A coordinate suffix .star file to select micrographs while inspecting coordinates (and/or CTFs).");

    joboptions["do_recenter"] = JobOption("Re-center the class averages?", true, "This option is only used when selecting particles from 2D classes. The selected class averages will all re-centered on their center-of-mass. This is useful when you plane to use these class averages as templates for auto-picking.");
    joboptions["do_regroup"] = JobOption("Regroup the particles?", false, "If set to Yes, then the program will regroup the selected particles in 'more-or-less' the number of groups indicated below. For re-grouping from individual particle _data.star files, a _model.star file with the same prefix should exist, i.e. the particle star file should be generated by relion_refine");
    joboptions["nr_groups"] = JobOption("Approximate nr of groups: ", 1, 50, 20, 1, "It is normal that the actual number of groups may deviate a little from this number. ");

    joboptions["do_select_values"] = JobOption("Select based on metadata values?", false, "If set to Yes, the job will be non-interactive and the selected star file will be based only on the value of the corresponding metadata label. Note that this option is only valid for micrographs or particles STAR files.");
    joboptions["select_label"] = JobOption("Metadata label for subset selection:", (string)"rlnCtfMaxResolution", "This column from the input STAR file will be used for the subset selection.");
    joboptions["select_minval"] = JobOption("Minimum metadata value:",  (string) "-9999.", "Only lines in the input STAR file with the corresponding metadata value larger than or equal to this value will be included in the subset.");
    joboptions["select_maxval"] = JobOption("Maximum metadata value:",  (string) "9999.", "Only lines in the input STAR file with the corresponding metadata value smaller than or equal to this value will be included in the subset.");

    joboptions["do_discard"] = JobOption("OR: select on image statistics?", false, "If set to Yes, the job will be non-interactive and all images in the input star file that have average and/or stddev pixel values that are more than the specified sigma-values away from the ensemble mean will be discarded.");
    joboptions["discard_label"] = JobOption("Metadata label for images:", (string) "rlnImageName", "Specify which column from the input STAR contains the names of the images to be used to calculate the average and stddev values.");
    joboptions["discard_sigma"] = JobOption("Sigma-value for discarding images:", 4, 1, 10, 0.1, "Images with average and/or stddev values that are more than this many times the ensemble stddev away from the ensemble mean will be discarded.");

    joboptions["do_split"] = JobOption("OR: split into subsets?", false, "If set to Yes, the job will be non-interactive and the star file will be split into subsets as defined below.");
    joboptions["do_random"] = JobOption("Randomise order before making subsets?:", false, "If set to Yes, the input STAR file order will be randomised. If set to No, the original order in the input STAR file will be maintained.");
    joboptions["split_size"] = JobOption("Subset size:", 100, 100, 10000, 100, "The number of lines in each of the output subsets. When this is -1, items are divided into a number of subsets specified in the next option.");
    joboptions["nr_split"] = JobOption("OR: number of subsets:", -1, 1, 50, 1, "Give a positive integer to specify into how many equal-sized subsets the data will be divided. When the subset size is also specified, only this number of subsets, each with the specified size, will be written, possibly missing some items. When this is -1, all items are used, generating as many subsets as necessary.");

    joboptions["do_remove_duplicates"] = JobOption("OR: remove duplicates?", false, "If set to Yes, duplicated particles that are within a given distance are removed leaving only one. Duplicated particles are sometimes generated when particles drift into the same position during alignment. They inflate and invalidate gold-standard FSC calculation.");
    joboptions["duplicate_threshold"] = JobOption("Minimum inter-particle distance (A)", 30, 0, 1000, 1, "Particles within this distance are removed leaving only one.");
    joboptions["image_angpix"] = JobOption("Pixel size before extraction (A)", -1, -1, 10, 0.01, "The pixel size of particles (relevant to rlnOriginX/Y) is read from the STAR file. When the pixel size of the original micrograph used for auto-picking and extraction (relevant to rlnCoordinateX/Y) is different, specify it here. In other words, this is the pixel size after binning during motion correction, but before down-sampling during extraction.");
}

string RelionJob::getCommandsSelectJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::CLASSSELECT_NAME, job_counter);
    string command;

    if (
        joboptions["fn_model"].getString().empty() && joboptions["fn_coords"].getString().empty() &&
        joboptions["fn_mic"]  .getString().empty() && joboptions["fn_data"]  .getString().empty()
    ) {
        // Nothing was selected...
        throw "Please select an input file.";
    }

    int c = 0;
    c += joboptions["do_select_values"].getBoolean();
    c += joboptions["do_discard"].getBoolean();
    c += joboptions["do_split"].getBoolean();
    c += joboptions["do_remove_duplicates"].getBoolean();
    if (c > 1)
    throw "You cannot do many tasks simultaneously...";

    if (joboptions["do_remove_duplicates"].getBoolean()) {
        // Remove duplicates
        command = "`which relion_star_handler`";

        if (
            !joboptions["fn_mic"]   .getString().empty() ||
            !joboptions["fn_model"] .getString().empty() ||
            !joboptions["fn_coords"].getString().empty()
        ) throw errorMsg("Duplicate removal is only possible for particle STAR files...");

        if (joboptions["fn_data"].getString().empty())
        throw errorMsg("Duplicate removal needs a particle STAR file...");

        inputNodes.emplace_back(joboptions["fn_data"].getString(), joboptions["fn_data"].node_type);
        command += " --i " + joboptions["fn_data"].getString();

        const FileName fn_out = outputname + "particles.star";
        outputNodes.emplace_back(fn_out, Node::PART_DATA);
        command += " --o " + fn_out;

        command += " --remove_duplicates " + joboptions["duplicate_threshold"].getString();
        if (joboptions["image_angpix"].getNumber() > 0)  // May throw
        command += " --image_angpix " + joboptions["image_angpix"].getString();

    } else if (
        joboptions["do_select_values"].getBoolean() ||
        joboptions["do_discard"]      .getBoolean() ||
        joboptions["do_split"]        .getBoolean()
    ) {
        // Value-based selection
        command = "`which relion_star_handler`";

        if (
            !joboptions["fn_model"].getString().empty() ||
            !joboptions["fn_coords"].getString().empty()
        ) throw errorMsg("Value-selection or subset splitting is only possible for micrograph or particle STAR files...");

        FileName fn_out;
        const FileName fn_mic  = joboptions["fn_mic"].getString();
        const FileName fn_data = joboptions["fn_data"].getString();
        if (!fn_mic.empty()) {
            inputNodes.emplace_back(fn_mic, joboptions["fn_mic"].node_type);
            command += " --i " + fn_mic;
            fn_out = outputname + "micrographs.star";
        } else if (!fn_data.empty()) {
            inputNodes.emplace_back(fn_data, joboptions["fn_data"].node_type);
            command += " --i " + fn_data;
            fn_out = outputname + "particles.star";
        }
        command += " --o " + fn_out;

        if (
            joboptions["do_select_values"].getBoolean() ||
            joboptions["do_discard"]      .getBoolean()
        ) {

            if (!fn_mic.empty()) {
                outputNodes.emplace_back(fn_out, Node::MICS);
            } else if (!fn_data.empty()) {
                outputNodes.emplace_back(fn_out, Node::PART_DATA);
            }

            if (joboptions["do_select_values"].getBoolean()) {
                command += " --select " + joboptions["select_label"].getString();
                command += " --minval " + joboptions["select_minval"].getString();
                command += " --maxval " + joboptions["select_maxval"].getString();
            } else if (joboptions["do_discard"].getBoolean()) {
                command += " --discard_on_stats ";
                command += " --discard_label " + joboptions["discard_label"].getString();
                command += " --discard_sigma " + joboptions["discard_sigma"].getString();
            }

        } else if (joboptions["do_split"].getBoolean()) {
            int nr_split = 0;
            command += " --split ";
            if (joboptions["do_random"].getBoolean()) {
                command += " --random_order ";
            }

            if (
                joboptions["nr_split"]  .getNumber() <= 0 &&  // May throw
                joboptions["split_size"].getNumber() <= 0 &&  // May throw
                !joboptions["nr_split"].isSchedulerVariable() &&
                !joboptions["split_size"].isSchedulerVariable()
            ) throw errorMsg("When splitting the input STAR file into subsets, set nr_split and/or split_size to a positive value");

            if (
                 joboptions["nr_split"].getNumber() > 0 &&  // May throw
                !joboptions["nr_split"].isSchedulerVariable()
            ) {
                nr_split = joboptions["nr_split"].getNumber();
                command += " --nr_split " + joboptions["nr_split"].getString();
            }

            if (
                 joboptions["split_size"].getNumber() > 0 &&  // May throw
                !joboptions["split_size"].isSchedulerVariable()
            ) {
                command += " --size_split " + joboptions["split_size"].getString();
            }

            // As of relion-3.1, star_handler will write out a star file with the output nodes, which will be read by the pipeliner
        }
    } else {
        // Interactive selection

        command = "`which relion_display`";

        // I/O
        if (!joboptions["fn_model"].getString().empty()) {

            command += " --gui --i " + joboptions["fn_model"].getString();
            inputNodes.emplace_back(joboptions["fn_model"].getString(), joboptions["fn_model"].node_type);

            const FileName fn_parts = outputname + "particles.star";
            command += " --allow_save --fn_parts " + fn_parts;
            outputNodes.emplace_back(fn_parts, Node::PART_DATA);

            // Only save the 2D class averages for 2D jobs
            FileName fnt = joboptions["fn_model"].getString();
            if (fnt.contains("Class2D/")) {
                const FileName fn_imgs = outputname + "class_averages.star";
                command += " --fn_imgs " + fn_imgs;
                outputNodes.emplace_back(fn_imgs, Node::REFS2D);

                if (joboptions["do_recenter"].getBoolean()) {
                    command += " --recenter ";
                }
            }
        } else if (joboptions["fn_mic"].getString() != "") {
            command += " --gui --i " + joboptions["fn_mic"].getString();
            inputNodes.emplace_back(joboptions["fn_mic"].getString(), joboptions["fn_mic"].node_type);

            const FileName fn_mics = outputname+"micrographs.star";
            command += " --allow_save --fn_imgs " + fn_mics;
            outputNodes.emplace_back(fn_mics, Node::MICS);
        } else if (joboptions["fn_data"].getString() != "") {
            command += " --gui --i " + joboptions["fn_data"].getString();
            inputNodes.emplace_back(joboptions["fn_data"].getString(), joboptions["fn_data"].node_type);

            const FileName fn_parts = outputname+"particles.star";
            command += " --allow_save --fn_imgs " + fn_parts;
            outputNodes.emplace_back(fn_parts, Node::PART_DATA);
        } else if  (joboptions["fn_coords"].getString() != "") {
            RelionJob manualpickjob;

            FileName fn_job = ".gui_manualpick";
            bool iscont = false;
            if (exists(fn_job + "job.star") || exists(fn_job + "run.job")) {
                manualpickjob.read(fn_job.c_str(), iscont, true); // true means do initialise
            } else {
                throw "You need to save 'Manual picking' job settings (using the Jobs menu) before you can display coordinate files.";
            }

            // Get the name of the micrograph STAR file from reading the suffix file
            FileName fn_suffix = joboptions["fn_coords"].getString();
            FileName fn_star;
            if (is_continue) {
                fn_star = outputname + "micrographs_selected.star";
            } else {
                std::ifstream in(fn_suffix.data(), std::ios_base::in);
                in >> fn_star ;
                in.close();
            }
            const FileName fn_dirs = fn_suffix.beforeLastOf("/") + "/";
            fn_suffix = fn_suffix.afterLastOf("/").without("coords_suffix_").withoutExtension();

            // Launch the manualpicker...
            command = "`which relion_manualpick` --i " + fn_star;
            inputNodes.emplace_back(joboptions["fn_coords"].getString(), joboptions["fn_coords"].node_type);

            command += " --odir " + fn_dirs;
            command += " --pickname " + fn_suffix;

            // The output selection
            const FileName fn_outstar = outputname + "micrographs_selected.star";
            outputNodes.emplace_back(fn_outstar, Node::MICS);
            command += " --allow_save  --selection " + fn_outstar;

            // All the stuff from the saved manualpickjob
            command += " --scale "          + manualpickjob.joboptions["micscale"].getString();
            command += " --sigma_contrast " + manualpickjob.joboptions["sigma_contrast"].getString();
            command += " --black "          + manualpickjob.joboptions["black_val"].getString();
            command += " --white "          + manualpickjob.joboptions["white_val"].getString();

            // These calls to getNumber() may throw
            if (manualpickjob.joboptions["lowpass"].getNumber() > 0.0)
            command += " --lowpass " + manualpickjob.joboptions["lowpass"].getString();

            if (manualpickjob.joboptions["highpass"].getNumber() > 0.0)
            command += " --highpass " + manualpickjob.joboptions["highpass"].getString();

            if (manualpickjob.joboptions["angpix"].getNumber() > 0.0)
            command += " --angpix " + manualpickjob.joboptions["angpix"].getString();

            command += " --ctf_scale " + manualpickjob.joboptions["ctfscale"].getString();

            command += " --particle_diameter " + manualpickjob.joboptions["diameter"].getString();

            if (manualpickjob.joboptions["do_color"].getBoolean()) {
                command += " --color_label " + manualpickjob.joboptions["color_label"].getString();
                command += " --blue "        + manualpickjob.joboptions["blue_value"] .getString();
                command += " --red "         + manualpickjob.joboptions["red_value"]  .getString();
                if (manualpickjob.joboptions["fn_color"].getString().length() > 0)
                    command += " --color_star " + manualpickjob.joboptions["fn_color"].getString();
            }

            // Other arguments for extraction
            command += " " + manualpickjob.joboptions["other_args"].getString();
        }
    }

    // Re-grouping
    if (joboptions["do_regroup"].getBoolean() && joboptions["fn_coords"].getString().empty()) {
        if (joboptions["fn_model"].getString().empty())
        throw "Re-grouping only works for model.star files...";

        command += " --regroup " + joboptions["nr_groups"].getString();
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseClass2DJob() {
    hidden_name = ".gui_class2d";

    joboptions["fn_img"] = JobOption("Input images STAR file:", Node::PART_DATA, "", "STAR files (*.star) \t Image stacks (not recommended, read help!) (*.{spi,mrcs})", "A STAR file with all images (and their metadata). \n \n Alternatively, you may give a Spider/MRC stack of 2D images, but in that case NO metadata can be included and thus NO CTF correction can be performed, \
nor will it be possible to perform noise spectra estimation or intensity scale corrections in image groups. Therefore, running RELION with an input stack will in general provide sub-optimal results and is therefore not recommended!! Use the Preprocessing procedure to get the input STAR file in a semi-automated manner. Read the RELION wiki for more information.");
    joboptions["fn_cont"] = JobOption("Continue from here: ", string(""), "STAR Files (*_optimiser.star)", "CURRENT_ODIR",  "Select the *_optimiser.star file for the iteration \
from which you want to continue a previous run. \
Note that the Output rootname of the continued run and the rootname of the previous run cannot be the same. \
If they are the same, the program will automatically add a '_ctX' to the output rootname, \
with X being the iteration from which one continues the previous run.");

    joboptions["do_ctf_correction"] = JobOption("Do CTF-correction?", true, "If set to Yes, CTFs will be corrected inside the MAP refinement. \
The resulting algorithm intrinsically implements the optimal linear, or Wiener filter. \
Note that CTF parameters for all images need to be given in the input STAR file. \
The command 'relion_refine --print_metadata_labels' will print a list of all possible metadata labels for that STAR file. \
See the RELION Wiki for more details.\n\n Also make sure that the correct pixel size (in Angstrom) is given above!)");
    joboptions["ctf_intact_first_peak"] = JobOption("Ignore CTFs until first peak?", false, "If set to Yes, then CTF-amplitude correction will \
only be performed from the first peak of each CTF onward. This can be useful if the CTF model is inadequate at the lowest resolution. \
Still, in general using higher amplitude contrast on the CTFs (e.g. 10-20%) often yields better results. \
Therefore, this option is not generally recommended: try increasing amplitude contrast (in your input STAR file) first!");

    joboptions["nr_classes"] = JobOption("Number of classes:", 1, 1, 50, 1, "The number of classes (K) for a multi-reference refinement. \
These classes will be made in an unsupervised manner from a single reference by division of the data into random subsets during the first iteration.");
    joboptions["tau_fudge"] = JobOption("Regularisation parameter T:", 2 , 0.1, 10, 0.1, "Bayes law strictly determines the relative weight between \
the contribution of the experimental data and the prior. However, in practice one may need to adjust this weight to put slightly more weight on \
the experimental data to allow optimal results. Values greater than 1 for this regularisation parameter (T in the JMB2011 paper) put more \
weight on the experimental data. Values around 2-4 have been observed to be useful for 3D refinements, values of 1-2 for 2D refinements. \
Too small values yield too-low resolution structures; too high values result in over-estimated resolutions, mostly notable by the apparition of high-frequency noise in the references.");
    joboptions["nr_iter"] = JobOption("Number of iterations:", 25, 1, 50, 1, "Number of iterations to be performed. \
Note that the current implementation of 2D class averaging and 3D classification does NOT comprise a convergence criterium. \
Therefore, the calculations will need to be stopped by the user if further iterations do not yield improvements in resolution or classes. \n\n \
Also note that upon restarting, the iteration number continues to be increased, starting from the final iteration in the previous run. \
The number given here is the TOTAL number of iterations. For example, if 10 iterations have been performed previously and one restarts to perform \
an additional 5 iterations (for example with a finer angular sampling), then the number given here should be 10+5=15.");
    joboptions["do_fast_subsets"] = JobOption("Use fast subsets (for large data sets)?", false, "If set to Yes, the first 5 iterations will be done with random subsets of only K*100 particles (K being the number of classes); the next 5 with K*300 particles, the next 5 with 30% of the data set; and the final ones with all data. This was inspired by a cisTEM implementation by Niko Grigorieff et al.");

    joboptions["particle_diameter"] = JobOption("Mask diameter (A):", 200, 0, 1000, 10, "The experimental images will be masked with a soft \
circular mask with this diameter. Make sure this radius is not set too small because that may mask away part of the signal! \
If set to a value larger than the image size no masking will be performed.\n\n\
The same diameter will also be used for a spherical mask of the reference structures if no user-provided mask is specified.");
    joboptions["do_zero_mask"] = JobOption("Mask individual particles with zeros?", true, "If set to Yes, then in the individual particles, \
the area outside a circle with the radius of the particle will be set to zeros prior to taking the Fourier transform. \
This will remove noise and therefore increase sensitivity in the alignment and classification. However, it will also introduce correlations \
between the Fourier components that are not modelled. When set to No, then the solvent area is filled with random noise, which prevents introducing correlations.\
High-resolution refinements (e.g. ribosomes or other large complexes in 3D auto-refine) tend to work better when filling the solvent area with random noise (i.e. setting this option to No), refinements of smaller complexes and most classifications go better when using zeros (i.e. setting this option to Yes).");
    joboptions["highres_limit"] = JobOption("Limit resolution E-step to (A): ", -1, -1, 20, 1, "If set to a positive number, then the expectation step (i.e. the alignment) will be done only including the Fourier components up to this resolution (in Angstroms). \
This is useful to prevent overfitting, as the classification runs in RELION are not to be guaranteed to be 100% overfitting-free (unlike the 3D auto-refine with its gold-standard FSC). In particular for very difficult data sets, e.g. of very small or featureless particles, this has been shown to give much better class averages. \
In such cases, values in the range of 7-12 Angstroms have proven useful.");

    joboptions["dont_skip_align"] = JobOption("Perform image alignment?", true, "If set to No, then rather than \
performing both alignment and classification, only classification will be performed. This allows the use of very focused masks.\
This requires that the optimal orientations of all particles are already stored in the input STAR file. ");
    joboptions["psi_sampling"] = JobOption("In-plane angular sampling:", 6.0, 0.5, 20, 0.5, "The sampling rate for the in-plane rotation angle (psi) in degrees. \
Using fine values will slow down the program. Recommended value for most 2D refinements: 5 degrees.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["offset_range"] = JobOption("Offset search range (pix):", 5, 0, 30, 1, "Probabilities will be calculated only for translations \
in a circle with this radius (in pixels). The center of this circle changes at every iteration and is placed at the optimal translation \
for each image in the previous iteration.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["offset_step"] = JobOption("Offset search step (pix):", 1, 0.1, 5, 0.1, "Translations will be sampled with this step-size (in pixels).\
Translational sampling is also done using the adaptive approach. \
Therefore, if adaptive=1, the translations will first be evaluated on a 2x coarser grid.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["allow_coarser"] = JobOption("Allow coarser sampling?", false, "If set to Yes, the program will use coarser angular and translational samplings if the estimated accuracies of the assignments is still low in the earlier iterations. This may speed up the calculations.");

    joboptions["do_helix"] = JobOption("Classify 2D helical segments?", false, "Set to Yes if you want to classify 2D helical segments. Note that the helical segments should come with priors of psi angles");
    joboptions["helical_tube_outer_diameter"] = JobOption("Tube diameter (A): ", 200, 100, 1000, 10, "Outer diameter (in Angstroms) of helical tubes. \
This value should be slightly larger than the actual width of the tubes. You may want to copy the value from previous particle extraction job. \
If negative value is provided, this option is disabled and ordinary circular masks will be applied. Sometimes '--dont_check_norm' option is useful to prevent errors in normalisation of helical segments.");
    joboptions["do_bimodal_psi"] = JobOption("Do bimodal angular searches?", true, "Do bimodal search for psi angles? \
Set to Yes if you want to classify 2D helical segments with priors of psi angles. The priors should be bimodal due to unknown polarities of the segments. \
Set to No if the 3D helix looks the same when rotated upside down. If it is set to No, ordinary angular searches will be performed.\n\nThis option will be invalid if you choose not to perform image alignment on 'Sampling' tab.");
    joboptions["range_psi"] = JobOption("Angular search range - psi (deg):", 6, 3, 30, 1, "Local angular searches will be performed \
within +/- the given amount (in degrees) from the psi priors estimated through helical segment picking. \
A range of 15 degrees is the same as sigma = 5 degrees. Note that the ranges of angular searches should be much larger than the sampling.\
\n\nThis option will be invalid if you choose not to perform image alignment on 'Sampling' tab.");
    joboptions["do_restrict_xoff"] = JobOption("Restrict helical offsets to rise:", true, "Set to Yes if you want to restrict the translational offsets along the helices to the rise of the helix given below. Set to No to allow free (conventional) translational offsets.");
    joboptions["helical_rise"] = JobOption("Helical rise (A):", 4.75, -1, 100, 1, "The helical rise (in Angstroms). Translational offsets along the helical axis will be limited from -rise/2 to +rise/2, with a flat prior.");


    joboptions["nr_pool"] = JobOption("Number of pooled particles:", 3, 1, 16, 1, "Particles are processed in individual batches by MPI followers. During each batch, a stack of particle images is only opened and closed once to improve disk access times. \
All particle images of a single batch are read into memory together. The size of these batches is at least one particle per thread used. The nr_pooled_particles parameter controls how many particles are read together for each thread. If it is set to 3 and one uses 8 threads, batches of 3x8=24 particles will be read together. \
This may improve performance on systems where disk access, and particularly metadata handling of disk access, is a problem. It has a modest cost of increased RAM usage.");
    joboptions["do_parallel_discio"] = JobOption("Use parallel disc I/O?", true, "If set to Yes, all MPI followers will read images from disc. \
Otherwise, only the leader will read images and send them through the network to the followers. Parallel file systems like gluster of fhgfs are good at parallel disc I/O. NFS may break with many followers reading in parallel. If your datasets contain particles with different box sizes, you have to say Yes.");
    joboptions["do_preread_images"] = JobOption("Pre-read all particles into RAM?", false, "If set to Yes, all particle images will be read into computer memory, which will greatly speed up calculations on systems with slow disk access. However, one should of course be careful with the amount of RAM available. \
Because particles are read in float-precision, it will take ( N * box_size * box_size * 4 / (1024 * 1024 * 1024) ) Giga-bytes to read N particles into RAM. For 100 thousand 200x200 images, that becomes 15Gb, or 60 Gb for the same number of 400x400 particles. \
Remember that running a single MPI follower on each node that runs as many threads as available cores will have access to all available RAM. \n \n If parallel disc I/O is set to No, then only the leader reads all particles into RAM and sends those particles through the network to the MPI followers during the refinement iterations.");
    const char *scratch_dir = getenv("RELION_SCRATCH_DIR");
    if (!scratch_dir) { scratch_dir = DEFAULT::SCRATCHDIR; }
    joboptions["scratch_dir"] = JobOption("Copy particles to scratch directory:", string(scratch_dir), "If a directory is provided here, then the job will create a sub-directory in it called relion_volatile. If that relion_volatile directory already exists, it will be wiped. Then, the program will copy all input particles into a large stack inside the relion_volatile subdirectory. \
Provided this directory is on a fast local drive (e.g. an SSD drive), processing in all the iterations will be faster. If the job finishes correctly, the relion_volatile directory will be wiped. If the job crashes, you may want to remove it yourself.");
    joboptions["do_combine_thru_disc"] = JobOption("Combine iterations through disc?", false, "If set to Yes, at the end of every iteration all MPI followers will write out a large file with their accumulated results. The MPI leader will read in all these files, combine them all, and write out a new file with the combined results. \
All MPI salves will then read in the combined results. This reduces heavy load on the network, but increases load on the disc I/O. \
This will affect the time it takes between the progress-bar in the expectation step reaching its end (the mouse gets to the cheese) and the start of the ensuing maximisation step. It will depend on your system setup which is most efficient.");

    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','. For example: '0,0:1,1:0,0:1,1'");
}

string RelionJob::getCommandsClass2DJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::CLASS2D_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
        "`which relion_refine_mpi`" : "`which relion_refine`";

    FileName fn_run = "run";
    if (is_continue) {

        if (joboptions["fn_cont"].getString().empty())
        throw errorMsg("empty field for continuation STAR file...");

        int pos_it = joboptions["fn_cont"].getString().rfind("_it");
        int pos_op = joboptions["fn_cont"].getString().rfind("_optimiser");
        if (pos_it < 0 || pos_op < 0)
        throw "Warning: invalid optimiser.star filename provided for continuation run!";

        int it = textToFloat(joboptions["fn_cont"].getString().substr(pos_it + 3, 6).c_str());
        fn_run += "_ct" + floatToString(it);
        command += " --continue " + joboptions["fn_cont"].getString();
    }

    command += " --o " + outputname + fn_run;

    int my_iter    = joboptions["nr_iter"].getNumber();  // May throw
    int my_classes = joboptions["nr_classes"].getNumber();  // May throw
    outputNodes = getOutputNodesRefine(outputname + fn_run, my_iter, my_classes, 2, 1);

    if (!is_continue) {
        if (joboptions["fn_img"].getString().empty())
        throw errorMsg("empty field for input STAR file...");

        command += " --i " + joboptions["fn_img"].getString();
        inputNodes.emplace_back(joboptions["fn_img"].getString(), joboptions["fn_img"].node_type);
    }

    // Always do compute stuff
    if (!joboptions["do_combine_thru_disc"].getBoolean())
        command += " --dont_combine_weights_via_disc";
    if (!joboptions["do_parallel_discio"].getBoolean())
        command += " --no_parallel_disc_io";
    if (joboptions["do_preread_images"].getBoolean())
        command += " --preread_images " ;
    else if (joboptions["scratch_dir"].getString() != "")
        command += " --scratch_dir " +  joboptions["scratch_dir"].getString();
    command += " --pool " + joboptions["nr_pool"].getString();
    // Takanori observed bad 2D classifications with pad1, so use pad2 always. Memory isnt a problem here anyway.
    command += " --pad 2 ";

    // CTF stuff
    if (!is_continue) {
        if (joboptions["do_ctf_correction"].getBoolean()) {
            command += " --ctf ";
            if (joboptions["ctf_intact_first_peak"].getBoolean())
                command += " --ctf_intact_first_peak ";
        }
    }

    // Optimisation
    command += " --iter " + joboptions["nr_iter"].getString();

    command += " --tau2_fudge " + joboptions["tau_fudge"].getString();
        command += " --particle_diameter " + joboptions["particle_diameter"].getString();

    if (!is_continue) {
        if (joboptions["do_fast_subsets"].getBoolean())
            command += " --fast_subsets ";

        command += " --K " + joboptions["nr_classes"].getString();
        // Always flatten the solvent
        command += " --flatten_solvent ";
        if (joboptions["do_zero_mask"].getBoolean())
            command += " --zero_mask ";

        if (joboptions["highres_limit"].getNumber() > 0)  // May throw
            command += " --strict_highres_exp " + joboptions["highres_limit"].getString();

    }

    // Sampling
    int iover = 1;
    command += " --oversampling " + floatToString((float) iover);

    if (!joboptions["dont_skip_align"].getBoolean()) {
        command += " --skip_align ";
    } else {

        // The sampling given in the GUI will be the oversampled one!
        command += " --psi_step " + floatToString(joboptions["psi_sampling"].getNumber() * pow(2.0, iover));

        // Offset range
        command += " --offset_range " + joboptions["offset_range"].getString();

        // The sampling given in the GUI will be the oversampled one!
        command += " --offset_step " + floatToString(joboptions["offset_step"].getNumber() * pow(2.0, iover));

        if (joboptions["allow_coarser"].getBoolean())
            command += " --allow_coarser_sampling";

    }

    // Helix
    if (joboptions["do_helix"].getBoolean()) {
        command += " --helical_outer_diameter " + joboptions["helical_tube_outer_diameter"].getString();

        if (joboptions["dont_skip_align"].getBoolean()) {
            if (joboptions["do_bimodal_psi"].getBoolean())
                command += " --bimodal_psi";

            RFLOAT range_psi = joboptions["range_psi"].getNumber();  // May throw
            if (range_psi <  0.0) { range_psi =  0.0; }
            if (range_psi > 90.0) { range_psi = 90.0; }
            command += " --sigma_psi " + floatToString(range_psi / 3.0);

            if (joboptions["do_restrict_xoff"].getBoolean())
                command += " --helix --helical_rise_initial " + joboptions["helical_rise"].getString();
        }
    }

    // Always do norm and scale correction
    if (!is_continue)
        command += " --norm --scale ";

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // GPU-stuff
    if (joboptions["use_gpu"].getBoolean()) {
        command += " --gpu \"" + joboptions["gpu_ids"].getString() +"\"";
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);
    return prepareFinalCommand(outputname, commands, do_makedir);
}

// Constructor for initial model job
void RelionJob::initialiseInimodelJob() {
    hidden_name = ".gui_inimodel";

    joboptions["fn_img"] = JobOption("Input images STAR file:", Node::PART_DATA, "", "STAR files (*.star) \t Image stacks (not recommended, read help!) (*.{spi,mrcs})", "A STAR file with all images (and their metadata). \
In SGD, it is very important that there are particles from enough different orientations. One only needs a few thousand to 10k particles. When selecting good 2D classes in the Subset Selection jobtype, use the option to select a maximum number of particles from each class to generate more even angular distributions for SGD.\
\n \n Alternatively, you may give a Spider/MRC stack of 2D images, but in that case NO metadata can be included and thus NO CTF correction can be performed, \
nor will it be possible to perform noise spectra estimation or intensity scale corrections in image groups. Therefore, running RELION with an input stack will in general provide sub-optimal results and is therefore not recommended!! Use the Preprocessing procedure to get the input STAR file in a semi-automated manner. Read the RELION wiki for more information.");
    joboptions["fn_cont"] = JobOption("Continue from here: ", string(""), "STAR Files (*_optimiser.star)", "CURRENT_ODIR", "Select the *_optimiser.star file for the iteration \
from which you want to continue a previous run. \
Note that the Output rootname of the continued run and the rootname of the previous run cannot be the same. \
If they are the same, the program will automatically add a '_ctX' to the output rootname, \
with X being the iteration from which one continues the previous run.");

    joboptions["sgd_ini_iter"] = JobOption("Number of initial iterations:", 50, 10, 300, 10, "Number of initial SGD iterations, at which the initial resolution cutoff and the initial subset size will be used, and multiple references are kept the same. 50 seems to work well in many cases. Increase if the correct solution is not found.");
    joboptions["sgd_inbetween_iter"] = JobOption("Number of in-between iterations:", 200, 50, 500, 50, "Number of SGD iterations between the initial and final ones. During these in-between iterations, the resolution is linearly increased, \
together with the mini-batch or subset size. In case of a multi-class refinement, the different references are also increasingly left to become dissimilar. 200 seems to work well in many cases. Increase if multiple references have trouble separating, or the correct solution is not found.");
    joboptions["sgd_fin_iter"] = JobOption("Number of final iterations:", 50, 10, 300, 10, "Number of final SGD iterations, at which the final resolution cutoff and the final subset size will be used, and multiple references are left dissimilar. 50 seems to work well in many cases. Perhaps increase when multiple reference have trouble separating.");

    joboptions["sgd_ini_resol"] = JobOption("Initial resolution (A):", 35, 10, 60, 5, "This is the resolution cutoff (in A) that will be applied during the initial SGD iterations. 35A seems to work well in many cases.");
    joboptions["sgd_fin_resol"] = JobOption("Final resolution (A):", 15, 5, 30, 5, "This is the resolution cutoff (in A) that will be applied during the final SGD iterations. 15A seems to work well in many cases.");

    joboptions["sgd_ini_subset_size"] = JobOption("Initial mini-batch size:", 100, 30, 300, 10, "The number of particles that will be processed during the initial iterations. 100 seems to work well in many cases. Lower values may result in wider searches of the energy landscape, but possibly at reduced resolutions.");
    joboptions["sgd_fin_subset_size"] = JobOption("Final mini-batch size:", 500, 100, 2000, 100, "The number of particles that will be processed during the final iterations. 300-500 seems to work well in many cases. Higher values may result in increased resolutions, but at increased computational costs and possibly reduced searches of the energy landscape, but possibly at reduced resolutions.");

    joboptions["sgd_write_iter"] = JobOption("Write-out frequency (iter):", 10, 1, 50, 1, "Every how many iterations do you want to write the model to disk?");

    joboptions["sgd_sigma2fudge_halflife"] = JobOption("Increased noise variance half-life:", -1, -100, 10000, 100, "When set to a positive value, the initial estimates of the noise variance will internally be multiplied by 8, and then be gradually reduced, \
having 50% after this many particles have been processed. By default, this option is switched off by setting this value to a negative number. \
In some difficult cases, switching this option on helps. In such cases, values around 1000 have been found to be useful. Change the factor of eight with the additional argument --sgd_sigma2fudge_ini");

    joboptions["nr_classes"] = JobOption("Number of classes:", 1, 1, 50, 1, "The number of classes (K) for a multi-reference ab initio SGD refinement. \
These classes will be made in an unsupervised manner, starting from a single reference in the initial iterations of the SGD, and the references will become increasingly dissimilar during the inbetween iterations.");
    joboptions["sym_name"] = JobOption("Symmetry:", string("C1"), "SGD sometimes works better in C1. If you make an initial model in C1 but want to run Class3D/Refine3D with a higher point group symmetry, the reference model must be rotated to conform the symmetry convention. You can do this by the relion_align_symmetry command.");
    joboptions["particle_diameter"] = JobOption("Mask diameter (A):", 200, 0, 1000, 10, "The experimental images will be masked with a soft \
circular mask with this diameter. Make sure this radius is not set too small because that may mask away part of the signal! \
If set to a value larger than the image size no masking will be performed.\n\n\
The same diameter will also be used for a spherical mask of the reference structures if no user-provided mask is specified.");
    joboptions["do_solvent"] = JobOption("Flatten and enforce non-negative solvent?", true, "If set to Yes, the job will apply a spherical mask and enforce all values in the reference to be non-negative.");

    //joboptions["do_zero_mask"] = JobOption("Mask individual particles with zeros?", true, "If set to Yes, then in the individual particles, \
the area outside a circle with the radius of the particle will be set to zeros prior to taking the Fourier transform. \
This will remove noise and therefore increase sensitivity in the alignment and classification. However, it will also introduce correlations \
between the Fourier components that are not modelled. When set to No, then the solvent area is filled with random noise, which prevents introducing correlations.\
High-resolution refinements (e.g. ribosomes or other large complexes in 3D auto-refine) tend to work better when filling the solvent area with random noise (i.e. setting this option to No), refinements of smaller complexes and most classifications go better when using zeros (i.e. setting this option to Yes).");

    joboptions["do_ctf_correction"] = JobOption("Do CTF-correction?", true, "If set to Yes, CTFs will be corrected inside the MAP refinement. \
The resulting algorithm intrinsically implements the optimal linear, or Wiener filter. \
Note that CTF parameters for all images need to be given in the input STAR file. \
The command 'relion_refine --print_metadata_labels' will print a list of all possible metadata labels for that STAR file. \
See the RELION Wiki for more details.\n\n Also make sure that the correct pixel size (in Angstrom) is given above!)");
    joboptions["ctf_intact_first_peak"] = JobOption("Ignore CTFs until first peak?", false, "If set to Yes, then CTF-amplitude correction will \
only be performed from the first peak of each CTF onward. This can be useful if the CTF model is inadequate at the lowest resolution. \
Still, in general using higher amplitude contrast on the CTFs (e.g. 10-20%) often yields better results. \
Therefore, this option is not generally recommended: try increasing amplitude contrast (in your input STAR file) first!");


    joboptions["sampling"] = JobOption("Initial angular sampling:", job_sampling_options, 1, "There are only a few discrete \
angular samplings possible because we use the HealPix library to generate the sampling of the first two Euler angles on the sphere. \
The samplings are approximate numbers and vary slightly over the sphere.\n\n For initial model generation at low resolutions, coarser angular samplings can often be used than in normal 3D classifications/refinements, e.g. 15 degrees. During the inbetween and final SGD iterations, the sampling will be adjusted to the resolution, given the particle size.");
    joboptions["offset_range"] = JobOption("Offset search range (pix):", 6, 0, 30, 1, "Probabilities will be calculated only for translations \
in a circle with this radius (in pixels). The center of this circle changes at every iteration and is placed at the optimal translation \
for each image in the previous iteration.\n\n");
    joboptions["offset_step"] = JobOption("Offset search step (pix):", 2, 0.1, 5, 0.1, "Translations will be sampled with this step-size (in pixels).\
Translational sampling is also done using the adaptive approach. \
Therefore, if adaptive=1, the translations will first be evaluated on a 2x coarser grid.\n\n ");

    joboptions["do_parallel_discio"] = JobOption("Use parallel disc I/O?", true, "If set to Yes, all MPI followers will read their own images from disc. \
Otherwise, only the leader will read images and send them through the network to the followers. Parallel file systems like gluster of fhgfs are good at parallel disc I/O. NFS may break with many followers reading in parallel. If your datasets contain particles with different box sizes, you have to say Yes.");
    joboptions["nr_pool"] = JobOption("Number of pooled particles:", 3, 1, 16, 1, "Particles are processed in individual batches by MPI followers. During each batch, a stack of particle images is only opened and closed once to improve disk access times. \
All particle images of a single batch are read into memory together. The size of these batches is at least one particle per thread used. The nr_pooled_particles parameter controls how many particles are read together for each thread. If it is set to 3 and one uses 8 threads, batches of 3x8=24 particles will be read together. \
This may improve performance on systems where disk access, and particularly metadata handling of disk access, is a problem. It has a modest cost of increased RAM usage.");
    joboptions["do_pad1"] = JobOption("Skip padding?", false, "If set to Yes, the calculations will not use padding in Fourier space for better interpolation in the references. Otherwise, references are padded 2x before Fourier transforms are calculated. Skipping padding (i.e. use --pad 1) gives nearly as good results as using --pad 2, but some artifacts may appear in the corners from signal that is folded back.");
    joboptions["skip_gridding"] = JobOption("Skip gridding?", true, "If set to Yes, the calculations will skip gridding in the M step to save time, typically with just as good results.");
    joboptions["do_preread_images"] = JobOption("Pre-read all particles into RAM?", false, "If set to Yes, all particle images will be read into computer memory, which will greatly speed up calculations on systems with slow disk access. However, one should of course be careful with the amount of RAM available. \
Because particles are read in float-precision, it will take ( N * box_size * box_size * 4 / (1024 * 1024 * 1024) ) Giga-bytes to read N particles into RAM. For 100 thousand 200x200 images, that becomes 15Gb, or 60 Gb for the same number of 400x400 particles. \
Remember that running a single MPI follower on each node that runs as many threads as available cores will have access to all available RAM. \n \n If parallel disc I/O is set to No, then only the leader reads all particles into RAM and sends those particles through the network to the MPI followers during the refinement iterations.");
    const char *scratch_dir = getenv("RELION_SCRATCH_DIR");
    if (!scratch_dir) { scratch_dir = DEFAULT::SCRATCHDIR; }
    joboptions["scratch_dir"] = JobOption("Copy particles to scratch directory:", string(scratch_dir), "If a directory is provided here, then the job will create a sub-directory in it called relion_volatile. If that relion_volatile directory already exists, it will be wiped. Then, the program will copy all input particles into a large stack inside the relion_volatile subdirectory. \
Provided this directory is on a fast local drive (e.g. an SSD drive), processing in all the iterations will be faster. If the job finishes correctly, the relion_volatile directory will be wiped. If the job crashes, you may want to remove it yourself.");
    joboptions["do_combine_thru_disc"] = JobOption("Combine iterations through disc?", false, "If set to Yes, at the end of every iteration all MPI followers will write out a large file with their accumulated results. The MPI leader will read in all these files, combine them all, and write out a new file with the combined results. \
All MPI salves will then read in the combined results. This reduces heavy load on the network, but increases load on the disc I/O. \
This will affect the time it takes between the progress-bar in the expectation step reaching its end (the mouse gets to the cheese) and the start of the ensuing maximisation step. It will depend on your system setup which is most efficient.");

    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','. For example: '0,0:1,1:0,0:1,1'");
}

string RelionJob::getCommandsInimodelJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();

    initialisePipeline(outputname, Process::INIMODEL_NAME, job_counter);

    string command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
        "`which relion_refine_mpi`" : "`which relion_refine`";

    FileName fn_run = "run";
    if (is_continue) {
        const FileName fn_cont = joboptions["fn_cont"].getString();
        if (fn_cont.empty())
        throw errorMsg("empty field for continuation STAR file...");
        const int pos_it = fn_cont.rfind("_it");
        const int pos_op = fn_cont.rfind("_optimiser");
        if (pos_it < 0 || pos_op < 0)
            std::cerr << "Warning: invalid optimiser.star filename provided for continuation run: " << fn_cont << std::endl;
        const int it = textToFloat(fn_cont.substr(pos_it + 3, 6).c_str());
        fn_run  += "_ct" + floatToString(it);
        command += " --continue " + fn_cont;
    }

    command += " --o " + outputname + fn_run;

    // getNumber() calls may throw
    int total_nr_iter = joboptions["sgd_ini_iter"].getNumber()
                      + joboptions["sgd_inbetween_iter"].getNumber()
                      + joboptions["sgd_fin_iter"].getNumber();

    int nr_classes = joboptions["nr_classes"].getNumber();

    outputNodes = getOutputNodesRefine(outputname + fn_run, total_nr_iter, nr_classes, 3, 1);

    command += " --sgd_ini_iter "       + joboptions["sgd_ini_iter"].getString();
    command += " --sgd_inbetween_iter " + joboptions["sgd_inbetween_iter"].getString();
    command += " --sgd_fin_iter "       + joboptions["sgd_fin_iter"].getString();
    command += " --sgd_write_iter "     + joboptions["sgd_write_iter"].getString();
    command += " --sgd_ini_resol "      + joboptions["sgd_ini_resol"].getString();
    command += " --sgd_fin_resol "      + joboptions["sgd_fin_resol"].getString();
    command += " --sgd_ini_subset "     + joboptions["sgd_ini_subset_size"].getString();
    command += " --sgd_fin_subset "     + joboptions["sgd_fin_subset_size"].getString();

    if (!is_continue) {
        command += " --sgd ";

        const FileName fn_img = joboptions["fn_img"].getString();
        if (fn_img.empty())
        throw errorMsg("empty field for input STAR file...");

        command += " --denovo_3dref --i " + fn_img;
        inputNodes.emplace_back(fn_img, joboptions["fn_img"].node_type);

        // CTF stuff
        #ifdef ALLOW_CTF_IN_SGD
        if (joboptions["do_ctf_correction"].getBoolean()) {
                command += " --ctf";
                if (joboptions["ctf_intact_first_peak"].getBoolean())
                        command += " --ctf_intact_first_peak";
        }
        #endif

        command += " --K " + joboptions["nr_classes"].getString();
        command += " --sym " + joboptions["sym_name"].getString();

        if (joboptions["do_solvent"].getBoolean())
            command += " --flatten_solvent ";
        command += " --zero_mask ";
    }

    // Always do compute stuff
    if (!joboptions["do_combine_thru_disc"].getBoolean())
        command += " --dont_combine_weights_via_disc";
    if (!joboptions["do_parallel_discio"].getBoolean())
        command += " --no_parallel_disc_io";
    if (joboptions["do_preread_images"].getBoolean()) {
        command += " --preread_images " ;
    } else if (!joboptions["scratch_dir"].getString().empty()) {
        command += " --scratch_dir " +  joboptions["scratch_dir"].getString();
    }
    command += " --pool " + joboptions["nr_pool"].getString();
    command += joboptions["do_pad1"].getBoolean() ? " --pad 1 " : " --pad 2 ";
    if (joboptions["skip_gridding"].getBoolean())
        command += " --skip_gridding ";

    // Optimisation
    command += " --particle_diameter " + joboptions["particle_diameter"].getString();

    // Sampling
    const int iover = 1;
    command += " --oversampling " + floatToString((float) iover);

    int sampling = JobOption::getHealPixOrder(joboptions["sampling"].getString());
    if (sampling <= 0)
    throw "Wrong choice for sampling";

    // The sampling given in the GUI will be the oversampled one!
    command += " --healpix_order " + floatToString(sampling - iover);

    // Offset range
    command += " --offset_range " + joboptions["offset_range"].getString();

    // The sampling given in the GUI will be the oversampled one!
    command += " --offset_step " + floatToString(joboptions["offset_step"].getNumber() * pow(2.0, iover));  // Why always 2?

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // GPU-stuff
    if (joboptions["use_gpu"].getBoolean()) {
        command += " --gpu \"" + joboptions["gpu_ids"].getString() +"\"";
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseClass3DJob() {
    hidden_name = ".gui_class3d";

    joboptions["fn_img"] = JobOption("Input images STAR file:", Node::PART_DATA, "", "STAR files (*.star) \t Image stacks (not recommended, read help!) (*.{spi,mrcs})", "A STAR file with all images (and their metadata). \n \n Alternatively, you may give a Spider/MRC stack of 2D images, but in that case NO metadata can be included and thus NO CTF correction can be performed, \
nor will it be possible to perform noise spectra estimation or intensity scale corrections in image groups. Therefore, running RELION with an input stack will in general provide sub-optimal results and is therefore not recommended!! Use the Preprocessing procedure to get the input STAR file in a semi-automated manner. Read the RELION wiki for more information.");
    joboptions["fn_cont"] = JobOption("Continue from here: ", string(""), "STAR Files (*_optimiser.star)", "CURRENT_ODIR", "Select the *_optimiser.star file for the iteration \
from which you want to continue a previous run. \
Note that the Output rootname of the continued run and the rootname of the previous run cannot be the same. \
If they are the same, the program will automatically add a '_ctX' to the output rootname, \
with X being the iteration from which one continues the previous run.");
    joboptions["fn_ref"] = JobOption("Reference map:", Node::REF3D, "", "Image Files (*.{spi,vol,mrc})", "A 3D map in MRC/Spider format. \
    Make sure this map has the same dimensions and the same pixel size as your input images.");
    joboptions["fn_mask"] = JobOption("Reference mask (optional):", Node::MASK, "", "Image Files (*.{spi,vol,msk,mrc})", "\
If no mask is provided, a soft spherical mask based on the particle diameter will be used.\n\
\n\
Otherwise, provide a Spider/mrc map containing a (soft) mask with the same \
dimensions as the reference(s), and values between 0 and 1, with 1 being 100% protein and 0 being 100% solvent. \
The reconstructed reference map will be multiplied by this mask.\n\
\n\
In some cases, for example for non-empty icosahedral viruses, it is also useful to use a second mask. For all white (value 1) pixels in this second mask \
the corresponding pixels in the reconstructed map are set to the average value of these pixels. \
Thereby, for example, the higher density inside the virion may be set to a constant. \
Note that this second mask should have one-values inside the virion and zero-values in the capsid and the solvent areas. \
To use a second mask, use the additional option --solvent_mask2, which may given in the Additional arguments line (in the Running tab).");

    joboptions["ref_correct_greyscale"] = JobOption("Ref. map is on absolute greyscale?", false, "Probabilities are calculated based on a Gaussian noise model, \
which contains a squared difference term between the reference and the experimental image. This has a consequence that the \
reference needs to be on the same absolute intensity grey-scale as the experimental images. \
RELION and XMIPP reconstruct maps at their absolute intensity grey-scale. \
Other packages may perform internal normalisations of the reference density, which will result in incorrect grey-scales. \
Therefore: if the map was reconstructed in RELION or in XMIPP, set this option to Yes, otherwise set it to No. \
If set to No, RELION will use a (grey-scale invariant) cross-correlation criterion in the first iteration, \
and prior to the second iteration the map will be filtered again using the initial low-pass filter. \
This procedure is relatively quick and typically does not negatively affect the outcome of the subsequent MAP refinement. \
Therefore, if in doubt it is recommended to set this option to No.");
    joboptions["ini_high"] = JobOption("Initial low-pass filter (A):", 60, 0, 200, 5, "It is recommended to strongly low-pass filter your initial reference map. \
If it has not yet been low-pass filtered, it may be done internally using this option. \
If set to 0, no low-pass filter will be applied to the initial reference(s).");
    joboptions["sym_name"] = JobOption("Symmetry:", string("C1"), "If the molecule is asymmetric, \
set Symmetry group to C1. Note their are multiple possibilities for icosahedral symmetry: \n \
* I1: No-Crowther 222 (standard in Heymann, Chagoyen & Belnap, JSB, 151 (2005) 196–207) \n \
* I2: Crowther 222 \n \
* I3: 52-setting (as used in SPIDER?)\n \
* I4: A different 52 setting \n \
The command 'relion_refine --sym D2 --print_symmetry_ops' prints a list of all symmetry operators for symmetry group D2. \
RELION uses XMIPP's libraries for symmetry operations. \
Therefore, look at the XMIPP Wiki for more details:  http://xmipp.cnb.csic.es/twiki/bin/view/Xmipp/WebHome?topic=Symmetry");

    joboptions["do_ctf_correction"] = JobOption("Do CTF-correction?", true, "If set to Yes, CTFs will be corrected inside the MAP refinement. \
The resulting algorithm intrinsically implements the optimal linear, or Wiener filter. \
Note that CTF parameters for all images need to be given in the input STAR file. \
The command 'relion_refine --print_metadata_labels' will print a list of all possible metadata labels for that STAR file. \
See the RELION Wiki for more details.\n\n Also make sure that the correct pixel size (in Angstrom) is given above!)");
    joboptions["ctf_corrected_ref"] = JobOption("Has reference been CTF-corrected?", false, "Set this option to Yes if the reference map \
represents density that is unaffected by CTF phases and amplitudes, e.g. it was created using CTF correction (Wiener filtering) inside RELION or from a PDB. \n\n\
If set to No, then in the first iteration, the Fourier transforms of the reference projections are not multiplied by the CTFs.");
    joboptions["ctf_intact_first_peak"] = JobOption("Ignore CTFs until first peak?", false, "If set to Yes, then CTF-amplitude correction will \
only be performed from the first peak of each CTF onward. This can be useful if the CTF model is inadequate at the lowest resolution. \
Still, in general using higher amplitude contrast on the CTFs (e.g. 10-20%) often yields better results. \
Therefore, this option is not generally recommended: try increasing amplitude contrast (in your input STAR file) first!");

    joboptions["nr_classes"] = JobOption("Number of classes:", 1, 1, 50, 1, "The number of classes (K) for a multi-reference refinement. \
These classes will be made in an unsupervised manner from a single reference by division of the data into random subsets during the first iteration.");
    joboptions["tau_fudge"] = JobOption("Regularisation parameter T:", 4 , 0.1, 10, 0.1, "Bayes law strictly determines the relative weight between \
the contribution of the experimental data and the prior. However, in practice one may need to adjust this weight to put slightly more weight on \
the experimental data to allow optimal results. Values greater than 1 for this regularisation parameter (T in the JMB2011 paper) put more \
weight on the experimental data. Values around 2-4 have been observed to be useful for 3D refinements, values of 1-2 for 2D refinements. \
Too small values yield too-low resolution structures; too high values result in over-estimated resolutions, mostly notable by the apparition of high-frequency noise in the references.");
    joboptions["nr_iter"] = JobOption("Number of iterations:", 25, 1, 50, 1, "Number of iterations to be performed. \
Note that the current implementation of 2D class averaging and 3D classification does NOT comprise a convergence criterium. \
Therefore, the calculations will need to be stopped by the user if further iterations do not yield improvements in resolution or classes. \n\n \
Also note that upon restarting, the iteration number continues to be increased, starting from the final iteration in the previous run. \
The number given here is the TOTAL number of iterations. For example, if 10 iterations have been performed previously and one restarts to perform \
an additional 5 iterations (for example with a finer angular sampling), then the number given here should be 10+5=15.");
    joboptions["do_fast_subsets"] = JobOption("Use fast subsets (for large data sets)?", false, "If set to Yes, the first 5 iterations will be done with random subsets of only K*1500 particles (K being the number of classes); the next 5 with K*4500 particles, the next 5 with 30% of the data set; and the final ones with all data. This was inspired by a cisTEM implementation by Niko Grigorieff et al.");

    joboptions["particle_diameter"] = JobOption("Mask diameter (A):", 200, 0, 1000, 10, "The experimental images will be masked with a soft \
circular mask with this diameter. Make sure this radius is not set too small because that may mask away part of the signal! \
If set to a value larger than the image size no masking will be performed.\n\n\
The same diameter will also be used for a spherical mask of the reference structures if no user-provided mask is specified.");
    joboptions["do_zero_mask"] = JobOption("Mask individual particles with zeros?", true, "If set to Yes, then in the individual particles, \
the area outside a circle with the radius of the particle will be set to zeros prior to taking the Fourier transform. \
This will remove noise and therefore increase sensitivity in the alignment and classification. However, it will also introduce correlations \
between the Fourier components that are not modelled. When set to No, then the solvent area is filled with random noise, which prevents introducing correlations.\
High-resolution refinements (e.g. ribosomes or other large complexes in 3D auto-refine) tend to work better when filling the solvent area with random noise (i.e. setting this option to No), refinements of smaller complexes and most classifications go better when using zeros (i.e. setting this option to Yes).");
    joboptions["highres_limit"] = JobOption("Limit resolution E-step to (A): ", -1, -1, 20, 1, "If set to a positive number, then the expectation step (i.e. the alignment) will be done only including the Fourier components up to this resolution (in Angstroms). \
This is useful to prevent overfitting, as the classification runs in RELION are not to be guaranteed to be 100% overfitting-free (unlike the 3D auto-refine with its gold-standard FSC). In particular for very difficult data sets, e.g. of very small or featureless particles, this has been shown to give much better class averages. \
In such cases, values in the range of 7-12 Angstroms have proven useful.");

    joboptions["dont_skip_align"] = JobOption("Perform image alignment?", true, "If set to No, then rather than \
performing both alignment and classification, only classification will be performed. This allows the use of very focused masks.\
This requires that the optimal orientations of all particles are already stored in the input STAR file. ");
    joboptions["sampling"] = JobOption("Angular sampling interval:", job_sampling_options, 2, "There are only a few discrete \
angular samplings possible because we use the HealPix library to generate the sampling of the first two Euler angles on the sphere. \
The samplings are approximate numbers and vary slightly over the sphere.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["offset_range"] = JobOption("Offset search range (pix):", 5, 0, 30, 1, "Probabilities will be calculated only for translations \
in a circle with this radius (in pixels). The center of this circle changes at every iteration and is placed at the optimal translation \
for each image in the previous iteration.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["offset_step"] = JobOption("Offset search step (pix):", 1, 0.1, 5, 0.1, "Translations will be sampled with this step-size (in pixels).\
Translational sampling is also done using the adaptive approach. \
Therefore, if adaptive=1, the translations will first be evaluated on a 2x coarser grid.\n\n \
If auto-sampling is used, this will be the value for the first iteration(s) only, and the sampling rate will be increased automatically after that.");
    joboptions["do_local_ang_searches"] = JobOption("Perform local angular searches?", false, "If set to Yes, then rather than \
performing exhaustive angular searches, local searches within the range given below will be performed. \
A prior Gaussian distribution centered at the optimal orientation in the previous iteration and \
with a stddev of 1/3 of the range given below will be enforced.");
    joboptions["sigma_angles"] = JobOption("Local angular search range:", 5.0, 0, 15, 0.1, "Local angular searches will be performed \
within +/- the given amount (in degrees) from the optimal orientation in the previous iteration. \
A Gaussian prior (also see previous option) will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.");
    joboptions["allow_coarser"] = JobOption("Allow coarser sampling?", false, "If set to Yes, the program will use coarser angular and translational samplings if the estimated accuracies of the assignments is still low in the earlier iterations. This may speed up the calculations.");
    joboptions["relax_sym"] = JobOption("Relax symmetry:", string(""), "With this option, poses related to the standard local angular search range by the given point group will also be explored. For example, if you have a pseudo-symmetric dimer A-A', refinement or classification in C1 with symmetry relaxation by C2 might be able to improve distinction between A and A'. Note that the reference must be more-or-less aligned to the convention of (pseudo-)symmetry operators. For details, see Ilca et al 2019 and Abrishami et al 2020 cited in the About dialog.");

    joboptions["do_helix"] = JobOption("Do helical reconstruction?", false, "If set to Yes, then perform 3D helical reconstruction.");
    joboptions["helical_tube_inner_diameter"] = JobOption("Tube diameter - inner (A):", string("-1"), "Inner and outer diameter (in Angstroms) of the reconstructed helix spanning across Z axis. \
Set the inner diameter to negative value if the helix is not hollow in the center. The outer diameter should be slightly larger than the actual width of helical tubes because it also decides the shape of 2D \
particle mask for each segment. If the psi priors of the extracted segments are not accurate enough due to high noise level or flexibility of the structure, then set the outer diameter to a large value.");
    joboptions["helical_tube_outer_diameter"] = JobOption("Tube diameter - outer (A):", string("-1"), "Inner and outer diameter (in Angstroms) of the reconstructed helix spanning across Z axis. \
Set the inner diameter to negative value if the helix is not hollow in the center. The outer diameter should be slightly larger than the actual width of helical tubes because it also decides the shape of 2D \
particle mask for each segment. If the psi priors of the extracted segments are not accurate enough due to high noise level or flexibility of the structure, then set the outer diameter to a large value.");
    joboptions["range_rot"] = JobOption("Angular search range - rot (deg):", string("-1"), "Local angular searches will be performed \
within +/- of the given amount (in degrees) from the optimal orientation in the previous iteration. The default negative value means that no local searches will be performed. \
A Gaussian prior will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["range_tilt"] = JobOption("Angular search range - tilt (deg):", string("15"), "Local angular searches will be performed \
within +/- the given amount (in degrees) from the optimal orientation in the previous iteration. \
A Gaussian prior (also see previous option) will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["range_psi"] = JobOption("Angular search range - psi (deg):", string("10"), "Local angular searches will be performed \
within +/- the given amount (in degrees) from the optimal orientation in the previous iteration. \
A Gaussian prior (also see previous option) will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["do_apply_helical_symmetry"] = JobOption("Apply helical symmetry?", true, "If set to Yes, helical symmetry will be applied in every iteration. Set to No if you have just started a project, helical symmetry is unknown or not yet estimated.");
    joboptions["helical_nr_asu"] = JobOption("Number of unique asymmetrical units:", 1, 1, 100, 1, "Number of unique helical asymmetrical units in each segment box. If the inter-box distance (set in segment picking step) \
is 100 Angstroms and the estimated helical rise is ~20 Angstroms, then set this value to 100 / 20 = 5 (nearest integer). This integer should not be less than 1. The correct value is essential in measuring the \
signal to noise ratio in helical reconstruction.");
    joboptions["helical_twist_initial"] =  JobOption("Initial helical twist (deg):", string("0"), "Initial helical symmetry. Set helical twist (in degrees) to positive value if it is a right-handed helix. \
Helical rise is a positive value in Angstroms. If local searches of helical symmetry are planned, initial values of helical twist and rise should be within their respective ranges.");
    joboptions["helical_rise_initial"] = JobOption("Initial helical rise (A):", string("0"), "Initial helical symmetry. Set helical twist (in degrees) to positive value if it is a right-handed helix. \
Helical rise is a positive value in Angstroms. If local searches of helical symmetry are planned, initial values of helical twist and rise should be within their respective ranges.");
    joboptions["helical_z_percentage"] = JobOption("Central Z length (%):", 30.0, 5.0, 80.0, 1.0, "Reconstructed helix suffers from inaccuracies of orientation searches. \
The central part of the box contains more reliable information compared to the top and bottom parts along Z axis, where Fourier artefacts are also present if the \
number of helical asymmetrical units is larger than 1. Therefore, information from the central part of the box is used for searching and imposing \
helical symmetry in real space. Set this value (%) to the central part length along Z axis divided by the box size. Values around 30% are commonly used.");
    joboptions["do_local_search_helical_symmetry"] = JobOption("Do local searches of symmetry?", false, "If set to Yes, then perform local searches of helical twist and rise within given ranges.");
    joboptions["helical_twist_min"] = JobOption("Helical twist search (deg) - Min:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_twist_max"] = JobOption("Helical twist search (deg) - Max:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_twist_inistep"] = JobOption("Helical twist search (deg) - Step:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_min"] = JobOption("Helical rise search (A) - Min:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_max"] = JobOption("Helical rise search (A) - Max:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_inistep"] = JobOption("Helical rise search (A) - Step:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_range_distance"] = JobOption("Range factor of local averaging:", -1.0, 1.0, 5.0, 0.1, "Local averaging of orientations and translations will be performed within a range of +/- this value * the box size. Polarities are also set to be the same for segments coming from the same tube during local refinement. \
Values of ~ 2.0 are recommended for flexible structures such as MAVS-CARD filaments, ParM, MamK, etc. This option might not improve the reconstructions of helices formed from curled 2D lattices (TMV and VipA/VipB). Set to negative to disable this option.");
    joboptions["keep_tilt_prior_fixed"] = JobOption("Keep tilt-prior fixed:", true, "If set to yes, the tilt prior will not change during the optimisation. If set to No, at each iteration the tilt prior will move to the optimal tilt value for that segment from the previous iteration.");

    joboptions["do_parallel_discio"] = JobOption("Use parallel disc I/O?", true, "If set to Yes, all MPI followers will read their own images from disc. \
Otherwise, only the leader will read images and send them through the network to the followers. Parallel file systems like gluster of fhgfs are good at parallel disc I/O. NFS may break with many followers reading in parallel. If your datasets contain particles with different box sizes, you have to say Yes.");
    joboptions["nr_pool"] = JobOption("Number of pooled particles:", 3, 1, 16, 1, "Particles are processed in individual batches by MPI followers. During each batch, a stack of particle images is only opened and closed once to improve disk access times. \
All particle images of a single batch are read into memory together. The size of these batches is at least one particle per thread used. The nr_pooled_particles parameter controls how many particles are read together for each thread. If it is set to 3 and one uses 8 threads, batches of 3x8=24 particles will be read together. \
This may improve performance on systems where disk access, and particularly metadata handling of disk access, is a problem. It has a modest cost of increased RAM usage.");
    joboptions["do_pad1"] = JobOption("Skip padding?", false, "If set to Yes, the calculations will not use padding in Fourier space for better interpolation in the references. Otherwise, references are padded 2x before Fourier transforms are calculated. Skipping padding (i.e. use --pad 1) gives nearly as good results as using --pad 2, but some artifacts may appear in the corners from signal that is folded back.");
    joboptions["skip_gridding"] = JobOption("Skip gridding?", true, "If set to Yes, the calculations will skip gridding in the M step to save time, typically with just as good results.");
    joboptions["do_preread_images"] = JobOption("Pre-read all particles into RAM?", false, "If set to Yes, all particle images will be read into computer memory, which will greatly speed up calculations on systems with slow disk access. However, one should of course be careful with the amount of RAM available. \
Because particles are read in float-precision, it will take ( N * box_size * box_size * 4 / (1024 * 1024 * 1024) ) Giga-bytes to read N particles into RAM. For 100 thousand 200x200 images, that becomes 15Gb, or 60 Gb for the same number of 400x400 particles. \
Remember that running a single MPI follower on each node that runs as many threads as available cores will have access to all available RAM. \n \n If parallel disc I/O is set to No, then only the leader reads all particles into RAM and sends those particles through the network to the MPI followers during the refinement iterations.");
    const char *scratch_dir = getenv("RELION_SCRATCH_DIR");
    if (!scratch_dir) { scratch_dir = DEFAULT::SCRATCHDIR; }
    joboptions["scratch_dir"] = JobOption("Copy particles to scratch directory:", string(scratch_dir), "If a directory is provided here, then the job will create a sub-directory in it called relion_volatile. If that relion_volatile directory already exists, it will be wiped. Then, the program will copy all input particles into a large stack inside the relion_volatile subdirectory. \
Provided this directory is on a fast local drive (e.g. an SSD drive), processing in all the iterations will be faster. If the job finishes correctly, the relion_volatile directory will be wiped. If the job crashes, you may want to remove it yourself.");
    joboptions["do_combine_thru_disc"] = JobOption("Combine iterations through disc?", false, "If set to Yes, at the end of every iteration all MPI followers will write out a large file with their accumulated results. The MPI leader will read in all these files, combine them all, and write out a new file with the combined results. \
All MPI salves will then read in the combined results. This reduces heavy load on the network, but increases load on the disc I/O. \
This will affect the time it takes between the progress-bar in the expectation step reaching its end (the mouse gets to the cheese) and the start of the ensuing maximisation step. It will depend on your system setup which is most efficient.");

    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','.  For example: '0,0:1,1:0,0:1,1'");
}

string RelionJob::getCommandsClass3DJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::CLASS3D_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
        "`which relion_refine_mpi`" : "`which relion_refine`";

    FileName fn_run = "run";
    if (is_continue) {
        const FileName fn_cont = joboptions["fn_cont"].getString();
        if (fn_cont.empty())
        throw errorMsg("empty field for continuation STAR file...");

        const int pos_it = fn_cont.rfind("_it");
        const int pos_op = fn_cont.rfind("_optimiser");
        if (pos_it < 0 || pos_op < 0)
            std::cerr << "Warning: invalid optimiser.star filename provided for continuation run: " << fn_cont << std::endl;
        const int it = textToFloat(fn_cont.substr(pos_it + 3, 6).c_str());
        fn_run += "_ct" + floatToString(it);
        command += " --continue " + fn_cont;
    }

    command += " --o " + outputname + fn_run;

    int nr_iter    = joboptions["nr_iter"].getNumber();  // May throw
    int nr_classes = joboptions["nr_classes"].getNumber();  // May throw
    outputNodes = getOutputNodesRefine(outputname + fn_run, nr_iter, nr_classes, 3, 1);

    if (!is_continue) {
        const FileName fn_img = joboptions["fn_img"].getString();
        if (fn_img.empty())
        throw errorMsg("empty field for input STAR file...");

        command += " --i " + fn_img;
        inputNodes.emplace_back(fn_img, joboptions["fn_img"].node_type);

        const FileName fn_ref = joboptions["fn_ref"].getString();
        if (fn_ref.empty())
        throw errorMsg("empty field for reference. Type None for de-novo subtomogram averaging, provide reference for single-particle analysis.");

        if (fn_ref != "None") {
            command += " --ref " + fn_ref;
            inputNodes.emplace_back(fn_ref, joboptions["fn_ref"].node_type);

            if (!joboptions["ref_correct_greyscale"].getBoolean()) // dont do firstiter_cc when giving None
                command += " --firstiter_cc";
        }

        if (joboptions["ini_high"].getNumber() > 0.0)  // May throw
        command += " --ini_high " + joboptions["ini_high"].getString();

    }

    // Always do compute stuff
    if (!joboptions["do_combine_thru_disc"].getBoolean())
        command += " --dont_combine_weights_via_disc";
    if (!joboptions["do_parallel_discio"].getBoolean())
        command += " --no_parallel_disc_io";
    if (joboptions["do_preread_images"].getBoolean()) {
        command += " --preread_images " ;
    } else if (!joboptions["scratch_dir"].getString().empty()) {
        command += " --scratch_dir " +  joboptions["scratch_dir"].getString();
    }
    command += " --pool " + joboptions["nr_pool"].getString();
    command += joboptions["do_pad1"].getBoolean() ? " --pad 1 " : " --pad 2 ";
    if (joboptions["skip_gridding"].getBoolean())
        command += " --skip_gridding ";

    // CTF stuff
    if (!is_continue) {
        if (joboptions["do_ctf_correction"].getBoolean()) {
            command += " --ctf";
            if (joboptions["ctf_corrected_ref"].getBoolean())
                command += " --ctf_corrected_ref";
            if (joboptions["ctf_intact_first_peak"].getBoolean())
                command += " --ctf_intact_first_peak";
        }
    }

    // Optimisation
    command += " --iter " + joboptions["nr_iter"].getString();
    command += " --tau2_fudge " + joboptions["tau_fudge"].getString();
    command += " --particle_diameter " + joboptions["particle_diameter"].getString();
    if (!is_continue) {
        if (joboptions["do_fast_subsets"].getBoolean())
            command += " --fast_subsets ";

        command += " --K " + joboptions["nr_classes"].getString();
        // Always flatten the solvent
        command += " --flatten_solvent";
        if (joboptions["do_zero_mask"].getBoolean())
            command += " --zero_mask";

        if (joboptions["highres_limit"].getNumber() > 0)  // May throw
        command += " --strict_highres_exp " + joboptions["highres_limit"].getString();

    }

    const FileName fn_mask = joboptions["fn_mask"].getString();
    if (fn_mask.length() > 0) {
        command += " --solvent_mask " + fn_mask;
        inputNodes.emplace_back(fn_mask, joboptions["fn_mask"].node_type);
    }

    // Sampling
    if (!joboptions["dont_skip_align"].getBoolean()) {
        command += " --skip_align ";
    } else {
        int iover = 1;
        command += " --oversampling " + floatToString((float) iover);
        int sampling = JobOption::getHealPixOrder(joboptions["sampling"].getString());
        if (sampling <= 0)
        throw "Wrong choice for sampling";

        // The sampling given in the GUI will be the oversampled one!
        command += " --healpix_order " + integerToString(sampling - iover);

        // Manually input local angular searches
        if (joboptions["do_local_ang_searches"].getBoolean()) {
            command += " --sigma_ang " + floatToString(joboptions["sigma_angles"].getNumber() / 3.0);  // May throw
            if (joboptions["relax_sym"].getString().length() > 0)
            command += " --relax_sym " + joboptions["relax_sym"].getString();
        }

        // Offset range
        command += " --offset_range " + joboptions["offset_range"].getString();
        // The sampling given in the GUI will be the oversampled one!
        command += " --offset_step " + floatToString(joboptions["offset_step"].getNumber() * pow(2.0, iover));  // May  throw

        if (joboptions["allow_coarser"].getBoolean())
            command += " --allow_coarser_sampling";

    }

    // Provide symmetry, and always do norm and scale correction
    if (!is_continue) {
        command += " --sym " + joboptions["sym_name"].getString();
        command += " --norm --scale ";
    }

    if (!is_continue && joboptions["do_helix"].getBoolean()) {
        command += " --helix";

        float inner_diam = joboptions["helical_tube_inner_diameter"].getNumber();  // May throw
        if (inner_diam > 0.0)
        command += " --helical_inner_diameter " + joboptions["helical_tube_inner_diameter"].getString();

        command += " --helical_outer_diameter " + joboptions["helical_tube_outer_diameter"].getString();

        if (joboptions["do_apply_helical_symmetry"].getBoolean()) {
            command += " --helical_nr_asu " + joboptions["helical_nr_asu"].getString();
            command += " --helical_twist_initial " + joboptions["helical_twist_initial"].getString();
            command += " --helical_rise_initial " + joboptions["helical_rise_initial"].getString();

            float myz = joboptions["helical_z_percentage"].getNumber() / 100.0;  // May throw
            command += " --helical_z_percentage " + floatToString(myz);

            if (joboptions["do_local_search_helical_symmetry"].getBoolean()) {
                command += " --helical_symmetry_search";
                command += " --helical_twist_min " + joboptions["helical_twist_min"].getString();
                command += " --helical_twist_max " + joboptions["helical_twist_max"].getString();

                float twist_inistep = joboptions["helical_twist_inistep"].getNumber();  // May throw
                if (twist_inistep > 0.0)
                command += " --helical_twist_inistep " + joboptions["helical_twist_inistep"].getString();

                command += " --helical_rise_min " + joboptions["helical_rise_min"].getString();
                command += " --helical_rise_max " + joboptions["helical_rise_max"].getString();

                float rise_inistep = joboptions["helical_rise_inistep"].getNumber();  // May throw
                if (rise_inistep > 0.0)
                command += " --helical_rise_inistep " + joboptions["helical_rise_inistep"].getString();

            }
        } else {
            command += " --ignore_helical_symmetry";
        }
        if (joboptions["keep_tilt_prior_fixed"].getBoolean())
            command += " --helical_keep_tilt_prior_fixed";
        if (joboptions["dont_skip_align"].getBoolean() && !joboptions["do_local_ang_searches"].getBoolean()) {

            // Any of these getNumber() calls may throw

            float range_tilt = joboptions["range_tilt"].getNumber();
            if (range_tilt <  0.0) { range_tilt =  0.0; }
            if (range_tilt > 90.0) { range_tilt = 90.0; }
            command += " --sigma_tilt " + floatToString(range_tilt / 3.0);

            float range_psi = joboptions["range_psi"].getNumber();
            if (range_psi <  0.0) { range_psi =  0.0; }
            if (range_psi > 90.0) { range_psi = 90.0; }
            command += " --sigma_psi " + floatToString(range_psi / 3.0);

            float range_rot = joboptions["range_rot"].getNumber();
            if (range_rot <  0.0) { range_rot =  0.0; }
            if (range_rot > 90.0) { range_rot = 90.0; }
            command += " --sigma_rot " + floatToString(range_rot / 3.0);

            float helical_range_distance = joboptions["helical_range_distance"].getNumber();
            if (helical_range_distance > 0.0)
            command += " --helical_sigma_distance " + floatToString(helical_range_distance / 3.0);

        }
    }

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // GPU-stuff
    if (joboptions["use_gpu"].getBoolean()) {
        if (!joboptions["dont_skip_align"].getBoolean())
        throw errorMsg("you cannot use GPUs when skipping image alignments.");

        command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseAutorefineJob() {
    type = Process::AUTO3D;

    hidden_name = ".gui_auto3d";

    joboptions["fn_img"] = JobOption("Input images STAR file:", Node::PART_DATA, "", "STAR files (*.star) \t Image stacks (not recommended, read help!) (*.{spi,mrcs})", "A STAR file with all images (and their metadata). \n \n Alternatively, you may give a Spider/MRC stack of 2D images, but in that case NO metadata can be included and thus NO CTF correction can be performed, \
nor will it be possible to perform noise spectra estimation or intensity scale corrections in image groups. Therefore, running RELION with an input stack will in general provide sub-optimal results and is therefore not recommended!! Use the Preprocessing procedure to get the input STAR file in a semi-automated manner. Read the RELION wiki for more information.");
    joboptions["fn_cont"] = JobOption("Continue from here: ", string(""), "STAR Files (*_optimiser.star)", "CURRENT_ODIR", "Select the *_optimiser.star file for the iteration \
from which you want to continue a previous run. \
Note that the Output rootname of the continued run and the rootname of the previous run cannot be the same. \
If they are the same, the program will automatically add a '_ctX' to the output rootname, \
with X being the iteration from which one continues the previous run.");
    joboptions["fn_ref"] = JobOption("Reference map:", Node::REF3D, "", "Image Files (*.{spi,vol,mrc})", "A 3D map in MRC/Spider format. \
    Make sure this map has the same dimensions and the same pixel size as your input images.");
    joboptions["fn_mask"] = JobOption("Reference mask (optional):", Node::MASK, "", "Image Files (*.{spi,vol,msk,mrc})", "\
If no mask is provided, a soft spherical mask based on the particle diameter will be used.\n\
\n\
Otherwise, provide a Spider/mrc map containing a (soft) mask with the same \
dimensions as the reference(s), and values between 0 and 1, with 1 being 100% protein and 0 being 100% solvent. \
The reconstructed reference map will be multiplied by this mask.\n\
\n\
In some cases, for example for non-empty icosahedral viruses, it is also useful to use a second mask. For all white (value 1) pixels in this second mask \
the corresponding pixels in the reconstructed map are set to the average value of these pixels. \
Thereby, for example, the higher density inside the virion may be set to a constant. \
Note that this second mask should have one-values inside the virion and zero-values in the capsid and the solvent areas. \
To use a second mask, use the additional option --solvent_mask2, which may given in the Additional arguments line (in the Running tab).");

    joboptions["ref_correct_greyscale"] = JobOption("Ref. map is on absolute greyscale?", false, "Probabilities are calculated based on a Gaussian noise model, \
which contains a squared difference term between the reference and the experimental image. This has a consequence that the \
reference needs to be on the same absolute intensity grey-scale as the experimental images. \
RELION and XMIPP reconstruct maps at their absolute intensity grey-scale. \
Other packages may perform internal normalisations of the reference density, which will result in incorrect grey-scales. \
Therefore: if the map was reconstructed in RELION or in XMIPP, set this option to Yes, otherwise set it to No. \
If set to No, RELION will use a (grey-scale invariant) cross-correlation criterion in the first iteration, \
and prior to the second iteration the map will be filtered again using the initial low-pass filter. \
This procedure is relatively quick and typically does not negatively affect the outcome of the subsequent MAP refinement. \
Therefore, if in doubt it is recommended to set this option to No.");
    joboptions["ini_high"] = JobOption("Initial low-pass filter (A):", 60, 0, 200, 5, "It is recommended to strongly low-pass filter your initial reference map. \
If it has not yet been low-pass filtered, it may be done internally using this option. \
If set to 0, no low-pass filter will be applied to the initial reference(s).");
    joboptions["sym_name"] = JobOption("Symmetry:", string("C1"), "If the molecule is asymmetric, \
set Symmetry group to C1. Note their are multiple possibilities for icosahedral symmetry: \n \
* I1: No-Crowther 222 (standard in Heymann, Chagoyen & Belnap, JSB, 151 (2005) 196–207) \n \
* I2: Crowther 222 \n \
* I3: 52-setting (as used in SPIDER?)\n \
* I4: A different 52 setting \n \
The command 'relion_refine --sym D2 --print_symmetry_ops' prints a list of all symmetry operators for symmetry group D2. \
RELION uses XMIPP's libraries for symmetry operations. \
Therefore, look at the XMIPP Wiki for more details:  http://xmipp.cnb.csic.es/twiki/bin/view/Xmipp/WebHome?topic=Symmetry");

    joboptions["do_ctf_correction"] = JobOption("Do CTF-correction?", true, "If set to Yes, CTFs will be applied to the projections of the map. This requires that CTF information is present in the input STAR file.");
    joboptions["ctf_corrected_ref"] = JobOption("Has reference been CTF-corrected?", false, "Set this option to Yes if the reference map \
represents density that is unaffected by CTF phases and amplitudes, e.g. it was created using CTF correction (Wiener filtering) inside RELION or from a PDB. \n\n\
If set to No, then in the first iteration, the Fourier transforms of the reference projections are not multiplied by the CTFs.");
    joboptions["ctf_intact_first_peak"] = JobOption("Ignore CTFs until first peak?", false, "If set to Yes, then CTF-amplitude correction will \
only be performed from the first peak of each CTF onward. This can be useful if the CTF model is inadequate at the lowest resolution. \
Still, in general using higher amplitude contrast on the CTFs (e.g. 10-20%) often yields better results. \
Therefore, this option is not generally recommended: try increasing amplitude contrast (in your input STAR file) first!");

    joboptions["particle_diameter"] = JobOption("Mask diameter (A):", 200, 0, 1000, 10, "The experimental images will be masked with a soft \
circular mask with this diameter. Make sure this radius is not set too small because that may mask away part of the signal! \
If set to a value larger than the image size no masking will be performed.\n\n\
The same diameter will also be used for a spherical mask of the reference structures if no user-provided mask is specified.");
    joboptions["do_zero_mask"] = JobOption("Mask individual particles with zeros?", true, "If set to Yes, then in the individual particles, \
the area outside a circle with the radius of the particle will be set to zeros prior to taking the Fourier transform. \
This will remove noise and therefore increase sensitivity in the alignment and classification. However, it will also introduce correlations \
between the Fourier components that are not modelled. When set to No, then the solvent area is filled with random noise, which prevents introducing correlations.\
High-resolution refinements (e.g. ribosomes or other large complexes in 3D auto-refine) tend to work better when filling the solvent area with random noise (i.e. setting this option to No), refinements of smaller complexes and most classifications go better when using zeros (i.e. setting this option to Yes).");
    joboptions["do_solvent_fsc"] = JobOption("Use solvent-flattened FSCs?", false, "If set to Yes, then instead of using unmasked maps to calculate the gold-standard FSCs during refinement, \
masked half-maps are used and a post-processing-like correction of the FSC curves (with phase-randomisation) is performed every iteration. This only works when a reference mask is provided on the I/O tab. \
This may yield higher-resolution maps, especially when the mask contains only a relatively small volume inside the box.");

    joboptions["sampling"] = JobOption("Initial angular sampling:", job_sampling_options, 2, "There are only a few discrete \
angular samplings possible because we use the HealPix library to generate the sampling of the first two Euler angles on the sphere. \
The samplings are approximate numbers and vary slightly over the sphere.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");
    joboptions["offset_range"] = JobOption("Initial offset range (pix):", 5, 0, 30, 1, "Probabilities will be calculated only for translations \
in a circle with this radius (in pixels). The center of this circle changes at every iteration and is placed at the optimal translation \
for each image in the previous iteration.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");
    joboptions["offset_step"] = JobOption("Initial offset step (pix):", 1, 0.1, 5, 0.1, "Translations will be sampled with this step-size (in pixels).\
Translational sampling is also done using the adaptive approach. \
Therefore, if adaptive=1, the translations will first be evaluated on a 2x coarser grid.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");
    joboptions["auto_local_sampling"] = JobOption("Local searches from auto-sampling:", job_sampling_options, 4, "In the automated procedure to \
increase the angular samplings, local angular searches of -6/+6 times the sampling rate will be used from this angular sampling rate onwards. For most \
lower-symmetric particles a value of 1.8 degrees will be sufficient. Perhaps icosahedral symmetries may benefit from a smaller value such as 0.9 degrees.");
    joboptions["relax_sym"] = JobOption("Relax symmetry:", string(""), "With this option, poses related to the standard local angular search range by the given point group will also be explored. For example, if you have a pseudo-symmetric dimer A-A', refinement or classification in C1 with symmetry relaxation by C2 might be able to improve distinction between A and A'. Note that the reference must be more-or-less aligned to the convention of (pseudo-)symmetry operators. For details, see Ilca et al 2019 and Abrishami et al 2020 cited in the About dialog.");
    joboptions["auto_faster"] = JobOption("Use finer angular sampling faster?", false, "If set to Yes, then let auto-refinement proceed faster with finer angular samplings. Two additional command-line options will be passed to the refine program: \n \n \
--auto_ignore_angles lets angular sampling go down despite changes still happening in the angles \n \n \
--auto_resol_angles lets angular sampling go down if the current resolution already requires that sampling at the edge of the particle.  \n\n \
This option will make the computation faster, but hasn't been tested for many cases for potential loss in reconstruction quality upon convergence.");

    joboptions["do_helix"] = JobOption("Do helical reconstruction?", false, "If set to Yes, then perform 3D helical reconstruction.");
    joboptions["helical_tube_inner_diameter"] = JobOption("Tube diameter - inner (A):", string("-1"), "Inner and outer diameter (in Angstroms) of the reconstructed helix spanning across Z axis. \
Set the inner diameter to negative value if the helix is not hollow in the center. The outer diameter should be slightly larger than the actual width of helical tubes because it also decides the shape of 2D \
particle mask for each segment. If the psi priors of the extracted segments are not accurate enough due to high noise level or flexibility of the structure, then set the outer diameter to a large value.");
    joboptions["helical_tube_outer_diameter"] = JobOption("Tube diameter - outer (A):", string("-1"), "Inner and outer diameter (in Angstroms) of the reconstructed helix spanning across Z axis. \
Set the inner diameter to negative value if the helix is not hollow in the center. The outer diameter should be slightly larger than the actual width of helical tubes because it also decides the shape of 2D \
particle mask for each segment. If the psi priors of the extracted segments are not accurate enough due to high noise level or flexibility of the structure, then set the outer diameter to a large value.");
    joboptions["range_rot"] = JobOption("Angular search range - rot (deg):", string("-1"), "Local angular searches will be performed \
within +/- of the given amount (in degrees) from the optimal orientation in the previous iteration. The default negative value means that no local searches will be performed. \
A Gaussian prior will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["range_tilt"] = JobOption("Angular search range - tilt (deg):", string("15"), "Local angular searches will be performed \
within +/- the given amount (in degrees) from the optimal orientation in the previous iteration. \
A Gaussian prior (also see previous option) will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["range_psi"] = JobOption("Angular search range - psi (deg):", string("10"), "Local angular searches will be performed \
within +/- the given amount (in degrees) from the optimal orientation in the previous iteration. \
A Gaussian prior (also see previous option) will be applied, so that orientations closer to the optimal orientation \
in the previous iteration will get higher weights than those further away.\n\nThese ranges will only be applied to the \
rot, tilt and psi angles in the first few iterations (global searches for orientations) in 3D helical reconstruction. \
Values of 9 or 15 degrees are commonly used. Higher values are recommended for more flexible structures and more memory and computation time will be used. \
A range of 15 degrees means sigma = 5 degrees.\n\nThese options will be invalid if you choose to perform local angular searches or not to perform image alignment on 'Sampling' tab.");
    joboptions["do_apply_helical_symmetry"] = JobOption("Apply helical symmetry?", true, "If set to Yes, helical symmetry will be applied in every iteration. Set to No if you have just started a project, helical symmetry is unknown or not yet estimated.");
    joboptions["helical_nr_asu"] = JobOption("Number of unique asymmetrical units:", 1, 1, 100, 1, "Number of unique helical asymmetrical units in each segment box. If the inter-box distance (set in segment picking step) \
is 100 Angstroms and the estimated helical rise is ~20 Angstroms, then set this value to 100 / 20 = 5 (nearest integer). This integer should not be less than 1. The correct value is essential in measuring the \
signal to noise ratio in helical reconstruction.");
    joboptions["helical_twist_initial"] =  JobOption("Initial helical twist (deg):", string("0"), "Initial helical symmetry. Set helical twist (in degrees) to positive value if it is a right-handed helix. \
Helical rise is a positive value in Angstroms. If local searches of helical symmetry are planned, initial values of helical twist and rise should be within their respective ranges.");
    joboptions["helical_rise_initial"] = JobOption("Initial helical rise (A):", string("0"), "Initial helical symmetry. Set helical twist (in degrees) to positive value if it is a right-handed helix. \
Helical rise is a positive value in Angstroms. If local searches of helical symmetry are planned, initial values of helical twist and rise should be within their respective ranges.");
    joboptions["helical_z_percentage"] = JobOption("Central Z length (%):", 30.0, 5.0, 80.0, 1.0, "Reconstructed helix suffers from inaccuracies of orientation searches. \
The central part of the box contains more reliable information compared to the top and bottom parts along Z axis, where Fourier artefacts are also present if the \
number of helical asymmetrical units is larger than 1. Therefore, information from the central part of the box is used for searching and imposing \
helical symmetry in real space. Set this value (%) to the central part length along Z axis divided by the box size. Values around 30% are commonly used.");
    joboptions["do_local_search_helical_symmetry"] = JobOption("Do local searches of symmetry?", false, "If set to Yes, then perform local searches of helical twist and rise within given ranges.");
    joboptions["helical_twist_min"] = JobOption("Helical twist search (deg) - Min:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_twist_max"] = JobOption("Helical twist search (deg) - Max:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_twist_inistep"] = JobOption("Helical twist search (deg) - Step:", string("0"), "Minimum, maximum and initial step for helical twist search. Set helical twist (in degrees) \
to positive value if it is a right-handed helix. Generally it is not necessary for the user to provide an initial step (less than 1 degree, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_min"] = JobOption("Helical rise search (A) - Min:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_max"] = JobOption("Helical rise search (A) - Max:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_rise_inistep"] = JobOption("Helical rise search (A) - Step:", string("0"), "Minimum, maximum and initial step for helical rise search. Helical rise is a positive value in Angstroms. \
Generally it is not necessary for the user to provide an initial step (less than 1% the initial helical rise, 5~1000 samplings as default). But it needs to be set manually if the default value \
does not guarantee convergence. The program cannot find a reasonable symmetry if the true helical parameters fall out of the given ranges. Note that the final reconstruction can still converge if wrong helical and point group symmetry are provided.");
    joboptions["helical_range_distance"] = JobOption("Range factor of local averaging:", -1.0, 1.0, 5.0, 0.1, "Local averaging of orientations and translations will be performed within a range of +/- this value * the box size. Polarities are also set to be the same for segments coming from the same tube during local refinement. \
Values of ~ 2.0 are recommended for flexible structures such as MAVS-CARD filaments, ParM, MamK, etc. This option might not improve the reconstructions of helices formed from curled 2D lattices (TMV and VipA/VipB). Set to negative to disable this option.");
    joboptions["keep_tilt_prior_fixed"] = JobOption("Keep tilt-prior fixed:", true, "If set to yes, the tilt prior will not change during the optimisation. If set to No, at each iteration the tilt prior will move to the optimal tilt value for that segment from the previous iteration.");

    joboptions["do_parallel_discio"] = JobOption("Use parallel disc I/O?", true, "If set to Yes, all MPI followers will read their own images from disc. \
Otherwise, only the leader will read images and send them through the network to the followers. Parallel file systems like gluster of fhgfs are good at parallel disc I/O. NFS may break with many followers reading in parallel. If your datasets contain particles with different box sizes, you have to say Yes.");
    joboptions["nr_pool"] = JobOption("Number of pooled particles:", 3, 1, 16, 1, "Particles are processed in individual batches by MPI followers. During each batch, a stack of particle images is only opened and closed once to improve disk access times. \
All particle images of a single batch are read into memory together. The size of these batches is at least one particle per thread used. The nr_pooled_particles parameter controls how many particles are read together for each thread. If it is set to 3 and one uses 8 threads, batches of 3x8=24 particles will be read together. \
This may improve performance on systems where disk access, and particularly metadata handling of disk access, is a problem. It has a modest cost of increased RAM usage.");
    joboptions["do_pad1"] = JobOption("Skip padding?", false, "If set to Yes, the calculations will not use padding in Fourier space for better interpolation in the references. Otherwise, references are padded 2x before Fourier transforms are calculated. Skipping padding (i.e. use --pad 1) gives nearly as good results as using --pad 2, but some artifacts may appear in the corners from signal that is folded back.");
    joboptions["skip_gridding"] = JobOption("Skip gridding?", true, "If set to Yes, the calculations will skip gridding in the M step to save time, typically with just as good results.");
    joboptions["do_preread_images"] = JobOption("Pre-read all particles into RAM?", false, "If set to Yes, all particle images will be read into computer memory, which will greatly speed up calculations on systems with slow disk access. However, one should of course be careful with the amount of RAM available. \
Because particles are read in float-precision, it will take ( N * box_size * box_size * 8 / (1024 * 1024 * 1024) ) Giga-bytes to read N particles into RAM. For 100 thousand 200x200 images, that becomes 15Gb, or 60 Gb for the same number of 400x400 particles. \
Remember that running a single MPI follower on each node that runs as many threads as available cores will have access to all available RAM. \n \n If parallel disc I/O is set to No, then only the leader reads all particles into RAM and sends those particles through the network to the MPI followers during the refinement iterations.");
    const char *scratch_dir = getenv("RELION_SCRATCH_DIR");
    if (!scratch_dir) { scratch_dir = DEFAULT::SCRATCHDIR; }
    joboptions["scratch_dir"] = JobOption("Copy particles to scratch directory:", string(scratch_dir), "If a directory is provided here, then the job will create a sub-directory in it called relion_volatile. If that relion_volatile directory already exists, it will be wiped. Then, the program will copy all input particles into a large stack inside the relion_volatile subdirectory. \
Provided this directory is on a fast local drive (e.g. an SSD drive), processing in all the iterations will be faster. If the job finishes correctly, the relion_volatile directory will be wiped. If the job crashes, you may want to remove it yourself.");
    joboptions["do_combine_thru_disc"] = JobOption("Combine iterations through disc?", false, "If set to Yes, at the end of every iteration all MPI followers will write out a large file with their accumulated results. The MPI leader will read in all these files, combine them all, and write out a new file with the combined results. \
All MPI salves will then read in the combined results. This reduces heavy load on the network, but increases load on the disc I/O. \
This will affect the time it takes between the progress-bar in the expectation step reaching its end (the mouse gets to the cheese) and the start of the ensuing maximisation step. It will depend on your system setup which is most efficient.");
    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','.  For example: '0,0:1,1:0,0:1,1'");
}

string RelionJob::getCommandsAutorefineJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::AUTO3D_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
        "`which relion_refine_mpi`" : "`which relion_refine`";

    FileName fn_run = "run";
    if (is_continue) {
        const std::string fn_cont = joboptions["fn_cont"].getString();
        if (fn_cont.empty())
        throw errorMsg("empty field for continuation STAR file...");

        const int pos_it = fn_cont.rfind("_it");
        const int pos_op = fn_cont.rfind("_optimiser");
        if (pos_it < 0 || pos_op < 0)
            std::cerr << "Warning: invalid optimiser.star filename provided for continuation run: " << fn_cont << std::endl;
        const int it = textToFloat(fn_cont.substr(pos_it + 3, 6).c_str());
        fn_run += "_ct" + floatToString(it);
        command += " --continue " + fn_cont;
    }

    command += " --o " + outputname + fn_run;
    // TODO: add bodies!! (probably in next version)
    outputNodes = getOutputNodesRefine(outputname + fn_run, -1, 1, 3, 1);

    if (!is_continue) {
        const FileName fn_img = joboptions["fn_img"].getString();
        command += " --auto_refine --split_random_halves --i " + fn_img;
        if (fn_img.empty())
        throw errorMsg("empty field for input STAR file...");

        inputNodes.emplace_back(fn_img, joboptions["fn_img"].node_type);

        const FileName fn_ref = joboptions["fn_ref"].getString();
        if (fn_ref.empty())
            throw errorMsg("empty field for input reference...");

        if (fn_ref != "None") {
            command += " --ref " + fn_ref;
            inputNodes.emplace_back(fn_ref, joboptions["fn_ref"].node_type);

            if (!joboptions["ref_correct_greyscale"].getBoolean())
                command += " --firstiter_cc";
        }

        if (joboptions["ini_high"].getNumber() > 0.0)  // May throw
        command += " --ini_high " + joboptions["ini_high"].getString();
    }

    // Always do compute stuff
    if (!joboptions["do_combine_thru_disc"].getBoolean())
        command += " --dont_combine_weights_via_disc";
    if (!joboptions["do_parallel_discio"].getBoolean())
        command += " --no_parallel_disc_io";
    if (joboptions["do_preread_images"].getBoolean())
        command += " --preread_images " ;
    else if (!joboptions["scratch_dir"].getString().empty())
        command += " --scratch_dir " +  joboptions["scratch_dir"].getString();
    command += " --pool " + joboptions["nr_pool"].getString();
    command += joboptions["do_pad1"].getBoolean() ? " --pad 1 " : " --pad 2 ";
    if (joboptions["skip_gridding"].getBoolean()) {
        command += " --skip_gridding ";
    }
    if (joboptions["auto_faster"].getBoolean()) {
        command += " --auto_ignore_angles --auto_resol_angles";
    }

    // CTF stuff
    if (!is_continue) {
        if (joboptions["do_ctf_correction"].getBoolean()) {
            command += " --ctf";
            if (joboptions["ctf_corrected_ref"].getBoolean())
                command += " --ctf_corrected_ref";
            if (joboptions["ctf_intact_first_peak"].getBoolean())
                command += " --ctf_intact_first_peak";
        }
    }

    // Optimisation
    command += " --particle_diameter " + joboptions["particle_diameter"].getString();
    if (!is_continue) {
        // Always flatten the solvent
        command += " --flatten_solvent";
        if (joboptions["do_zero_mask"].getBoolean())
            command += " --zero_mask";
    }
    const std::string fn_mask = joboptions["fn_mask"].getString();
    if (!fn_mask.empty()) {
        command += " --solvent_mask " + fn_mask;

        if (joboptions["do_solvent_fsc"].getBoolean())
            command += " --solvent_correct_fsc ";

        inputNodes.emplace_back(fn_mask, joboptions["fn_mask"].node_type);
    }

    if (!is_continue) {
        // Sampling
        int iover = 1;
        command += " --oversampling " + floatToString((float)iover);

        int sampling = JobOption::getHealPixOrder(joboptions["sampling"].getString());
        if (sampling <= 0)
        throw "Wrong choice for sampling";

        // The sampling given in the GUI will be the oversampled one!
        command += " --healpix_order " + integerToString(sampling - iover);

        // Minimum sampling rate to perform local searches (may be changed upon continuation
        int auto_local_sampling = JobOption::getHealPixOrder(joboptions["auto_local_sampling"].getString());
        if (auto_local_sampling <= 0)
        throw "Wrong choice for auto_local_sampling";

        // The sampling given in the GUI will be the oversampled one!
        command += " --auto_local_healpix_order " + integerToString(auto_local_sampling - iover);

        // Offset range
        command += " --offset_range " + joboptions["offset_range"].getString();
        // The sampling given in the GUI will be the oversampled one!
        command += " --offset_step " + floatToString(joboptions["offset_step"].getNumber() * pow(2.0, iover));  // May throw

        command += " --sym " + joboptions["sym_name"].getString();
        // Always join low-res data, as some D&I point group refinements may fall into different hands!
        command += " --low_resol_join_halves 40";
        command += " --norm --scale ";

        // Helix
        if (joboptions["do_helix"].getBoolean()) {

            command += " --helix";

            float inner_diam = joboptions["helical_tube_inner_diameter"].getNumber();  // May throw
            if (inner_diam > 0.0)
            command += " --helical_inner_diameter " + joboptions["helical_tube_inner_diameter"].getString();

            command += " --helical_outer_diameter " + joboptions["helical_tube_outer_diameter"].getString();
            if (joboptions["do_apply_helical_symmetry"].getBoolean()) {
                command += " --helical_nr_asu " + joboptions["helical_nr_asu"].getString();
                command += " --helical_twist_initial " + joboptions["helical_twist_initial"].getString();
                command += " --helical_rise_initial " + joboptions["helical_rise_initial"].getString();

                float myz = joboptions["helical_z_percentage"].getNumber() / 100.0;  // May throw
                command += " --helical_z_percentage " + floatToString(myz);

                if (joboptions["do_local_search_helical_symmetry"].getBoolean()) {
                    command += " --helical_symmetry_search";
                    command += " --helical_twist_min " + joboptions["helical_twist_min"].getString();
                    command += " --helical_twist_max " + joboptions["helical_twist_max"].getString();

                    float twist_inistep = joboptions["helical_twist_inistep"].getNumber();  // May throw
                    if (twist_inistep > 0.0)
                    command += " --helical_twist_inistep " + joboptions["helical_twist_inistep"].getString();

                    command += " --helical_rise_min " + joboptions["helical_rise_min"].getString();
                    command += " --helical_rise_max " + joboptions["helical_rise_max"].getString();

                    float rise_inistep = joboptions["helical_rise_inistep"].getNumber();  // May throw
                    if (rise_inistep > 0.0)
                    command += " --helical_rise_inistep " + joboptions["helical_rise_inistep"].getString();

                }
            } else {
                command += " --ignore_helical_symmetry";
            }

            if (sampling != auto_local_sampling) {
                // Any of these getNumber() calls may throw

                float range_tilt = joboptions["range_tilt"].getNumber();
                if (range_tilt <  0.0) { range_tilt =  0.0; }
                if (range_tilt > 90.0) { range_tilt = 90.0; }
                command += " --sigma_tilt " + floatToString(range_tilt / 3.0);

                float range_psi = joboptions["range_psi"].getNumber();
                if (range_psi <  0.0) { range_psi =  0.0; }
                if (range_psi > 90.0) { range_psi = 90.0; }
                command += " --sigma_psi " + floatToString(range_psi / 3.0);

                float range_rot = joboptions["range_rot"].getNumber();
                if (range_rot <  0.0) { range_rot =  0.0; }
                if (range_rot > 90.0) { range_rot = 90.0; }
                command += " --sigma_rot " + floatToString(range_rot / 3.0);

            }

            float helical_range_distance = joboptions["helical_range_distance"].getNumber();   // May throw
            if (helical_range_distance > 0.0)
            command += " --helical_sigma_distance " + floatToString(helical_range_distance / 3.0);

            if (joboptions["keep_tilt_prior_fixed"].getBoolean())
                command += " --helical_keep_tilt_prior_fixed";
        }
    }

    if (joboptions["relax_sym"].getString().length() > 0)
        command += " --relax_sym " + joboptions["relax_sym"].getString();

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // GPU-stuff
    if (joboptions["use_gpu"].getBoolean()) {
        command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseMultiBodyJob() {
    type = Process::MULTIBODY;

    hidden_name = ".gui_multibody";

    joboptions["fn_in"] = JobOption("Consensus refinement optimiser.star: ", string(""), "STAR Files (*_optimiser.star)", "Refine3D/", "Select the *_optimiser.star file for the iteration of the consensus refinement \
from which you want to start multi-body refinement.");

    joboptions["fn_cont"] = JobOption("Continue from here: ", string(""), "STAR Files (*_optimiser.star)", "CURRENT_ODIR", "Select the *_optimiser.star file for the iteration \
from which you want to continue this multi-body refinement. \
Note that the Output rootname of the continued run and the rootname of the previous run cannot be the same. \
If they are the same, the program will automatically add a '_ctX' to the output rootname, \
with X being the iteration from which one continues the previous run.");

    joboptions["fn_bodies"] = JobOption("Body STAR file:", string(""), "STAR Files (*.{star})", ".", " Provide the STAR file with all information about the bodies to be used in multi-body refinement. \
An example for a three-body refinement would look like this: \n\
\n\
data_\n\
loop_\n\
_rlnBodyMaskName\n\
_rlnBodyRotateRelativeTo\n\
_rlnBodySigmaAngles\n\
_rlnBodySigmaOffset\n\
large_body_mask.mrc 2 10 2\n\
small_body_mask.mrc 1 10 2\n\
head_body_mask.mrc 2 10 2\n\
\n\
Where each data line represents a different body, and: \n \
 - rlnBodyMaskName contains the name of a soft-edged mask with values in [0,1] that define the body; \n\
 - rlnBodyRotateRelativeTo defines relative to which other body this body rotates (first body is number 1); \n\
 - rlnBodySigmaAngles and _rlnBodySigmaOffset are the standard deviations (widths) of Gaussian priors on the consensus rotations and translations; \n\
\n \
Optionally, there can be a fifth column with _rlnBodyReferenceName. Entries can be 'None' (without the ''s) or the name of a MRC map with an initial reference for that body. In case the entry is None, the reference will be taken from the density in the consensus refinement.\n \n\
Also note that larger bodies should be above smaller bodies in the STAR file. For more information, see the multi-body paper.");

    joboptions["do_subtracted_bodies"] = JobOption("Reconstruct subtracted bodies?", true, "If set to Yes, then the reconstruction of each of the bodies will use the subtracted images. This may give \
useful insights about how well the subtraction worked. If set to No, the original particles are used for reconstruction (while the subtracted ones are still used for alignment). This will result in fuzzy densities for bodies outside the one used for refinement.");

    joboptions["sampling"] = JobOption("Initial angular sampling:", job_sampling_options, 4, "There are only a few discrete \
angular samplings possible because we use the HealPix library to generate the sampling of the first two Euler angles on the sphere. \
The samplings are approximate numbers and vary slightly over the sphere.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");
    joboptions["offset_range"] = JobOption("Initial offset range (pix):", 3, 0, 30, 1, "Probabilities will be calculated only for translations \
in a circle with this radius (in pixels). The center of this circle changes at every iteration and is placed at the optimal translation \
for each image in the previous iteration.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");
    joboptions["offset_step"] = JobOption("Initial offset step (pix):", 0.75, 0.1, 5, 0.1, "Translations will be sampled with this step-size (in pixels).\
Translational sampling is also done using the adaptive approach. \
Therefore, if adaptive=1, the translations will first be evaluated on a 2x coarser grid.\n\n \
Note that this will only be the value for the first few iteration(s): the sampling rate will be increased automatically after that.");


    joboptions["do_analyse"] = JobOption("Run flexibility analysis?", true, "If set to Yes, after the multi-body refinement has completed, a PCA analysis will be run on the orientations all all bodies in the data set. This can be set to No initially, and then the job can be continued afterwards to only perform this analysis.");
    joboptions["nr_movies"] = JobOption("Number of eigenvector movies:", 3, 0, 16, 1, "Series of ten output maps will be generated along this many eigenvectors. These maps can be opened as a 'Volume Series' in UCSF Chimera, and then displayed as a movie. They represent the principal motions in the particles.");
    joboptions["do_select"] = JobOption("Select particles based on eigenvalues?", false, "If set to Yes, a particles.star file is written out with all particles that have the below indicated eigenvalue in the selected range.");
    joboptions["select_eigenval"] = JobOption("Select on eigenvalue:", 1, 1, 20, 1, "This is the number of the eigenvalue to be used in the particle subset selection (start counting at 1).");
    joboptions["eigenval_min"] = JobOption("Minimum eigenvalue:", -999.0, -50, 50, 1, "This is the minimum value for the selected eigenvalue; only particles with the selected eigenvalue larger than this value will be included in the output particles.star file");
    joboptions["eigenval_max"] = JobOption("Maximum eigenvalue:", 999.0, -50, 50, 1, "This is the maximum value for the selected eigenvalue; only particles with the selected eigenvalue less than this value will be included in the output particles.star file");

    joboptions["do_parallel_discio"] = JobOption("Use parallel disc I/O?", true, "If set to Yes, all MPI followers will read their own images from disc. \
Otherwise, only the leader will read images and send them through the network to the followers. Parallel file systems like gluster of fhgfs are good at parallel disc I/O. NFS may break with many followers reading in parallel. If your datasets contain particles with different box sizes, you have to say Yes.");
    joboptions["nr_pool"] = JobOption("Number of pooled particles:", 3, 1, 16, 1, "Particles are processed in individual batches by MPI followers. During each batch, a stack of particle images is only opened and closed once to improve disk access times. \
All particle images of a single batch are read into memory together. The size of these batches is at least one particle per thread used. The nr_pooled_particles parameter controls how many particles are read together for each thread. If it is set to 3 and one uses 8 threads, batches of 3x8=24 particles will be read together. \
This may improve performance on systems where disk access, and particularly metadata handling of disk access, is a problem. It has a modest cost of increased RAM usage.");
    joboptions["do_pad1"] = JobOption("Skip padding?", false, "If set to Yes, the calculations will not use padding in Fourier space for better interpolation in the references. Otherwise, references are padded 2x before Fourier transforms are calculated. Skipping padding (i.e. use --pad 1) gives nearly as good results as using --pad 2, but some artifacts may appear in the corners from signal that is folded back.");
    joboptions["skip_gridding"] = JobOption("Skip gridding?", true, "If set to Yes, the calculations will skip gridding in the M step to save time, typically with just as good results.");
    joboptions["do_preread_images"] = JobOption("Pre-read all particles into RAM?", false, "If set to Yes, all particle images will be read into computer memory, which will greatly speed up calculations on systems with slow disk access. However, one should of course be careful with the amount of RAM available. \
Because particles are read in float-precision, it will take ( N * box_size * box_size * 8 / (1024 * 1024 * 1024) ) Giga-bytes to read N particles into RAM. For 100 thousand 200x200 images, that becomes 15Gb, or 60 Gb for the same number of 400x400 particles. \
Remember that running a single MPI follower on each node that runs as many threads as available cores will have access to all available RAM. \n \n If parallel disc I/O is set to No, then only the leader reads all particles into RAM and sends those particles through the network to the MPI followers during the refinement iterations.");
    const char *scratch_dir = getenv("RELION_SCRATCH_DIR");
    if (!scratch_dir) { scratch_dir = DEFAULT::SCRATCHDIR; }
    joboptions["scratch_dir"] = JobOption("Copy particles to scratch directory:", string(scratch_dir), "If a directory is provided here, then the job will create a sub-directory in it called relion_volatile. If that relion_volatile directory already exists, it will be wiped. Then, the program will copy all input particles into a large stack inside the relion_volatile subdirectory. \
Provided this directory is on a fast local drive (e.g. an SSD drive), processing in all the iterations will be faster. If the job finishes correctly, the relion_volatile directory will be wiped. If the job crashes, you may want to remove it yourself.");
    joboptions["do_combine_thru_disc"] = JobOption("Combine iterations through disc?", false, "If set to Yes, at the end of every iteration all MPI followers will write out a large file with their accumulated results. The MPI leader will read in all these files, combine them all, and write out a new file with the combined results. \
All MPI salves will then read in the combined results. This reduces heavy load on the network, but increases load on the disc I/O. \
This will affect the time it takes between the progress-bar in the expectation step reaching its end (the mouse gets to the cheese) and the start of the ensuing maximisation step. It will depend on your system setup which is most efficient.");
    joboptions["use_gpu"] = JobOption("Use GPU acceleration?", false, "If set to Yes, the job will try to use GPU acceleration.");
    joboptions["gpu_ids"] = JobOption("Which GPUs to use:", string(""), "This argument is not necessary. If left empty, the job itself will try to allocate available GPU resources. You can override the default allocation by providing a list of which GPUs (0,1,2,3, etc) to use. MPI-processes are separated by ':', threads by ','.  For example: '0,0:1,1:0,0:1,1'");
}

string RelionJob::getCommandsMultiBodyJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::MULTIBODY_NAME, job_counter);

    if (!exists(joboptions["fn_bodies"].getString()))
    throw errorMsg("you have to specify an existing body STAR file.");

    if (
        is_continue &&
        joboptions["fn_cont"].getString().empty() &&
        !joboptions["do_analyse"].getBoolean()
    ) throw errorMsg("either specify an optimiser file to continue multibody refinement from; OR run flexibility analysis...");

    string command;
    FileName fn_run = "";
    if (!is_continue || !joboptions["fn_cont"].getString().empty()) {

        command = joboptions["nr_mpi"].getNumber() > 1 ?  // May throw
            "`which relion_refine_mpi`" : "`which relion_refine`";

        MetaDataTable MD;
        MD.read(joboptions["fn_bodies"].getString());
        int nr_bodies = MD.size();

        if (is_continue) {
            int pos_it = joboptions["fn_cont"].getString().rfind("_it");
            int pos_op = joboptions["fn_cont"].getString().rfind("_optimiser");
            if (pos_it < 0 || pos_op < 0)
                std::cerr << "Warning: invalid optimiser.star filename provided for continuation run: " << joboptions["fn_cont"].getString() << std::endl;
            int it = (int) textToFloat((joboptions["fn_cont"].getString().substr(pos_it + 3, 6)).c_str());
            fn_run = "run_ct" + floatToString(it);
            command += " --continue " + joboptions["fn_cont"].getString();
            command += " --o " + outputname + fn_run;
            outputNodes = getOutputNodesRefine(outputname + fn_run, -1, 1, 3, nr_bodies);
        } else {
            fn_run = "run";
            command += " --continue " + joboptions["fn_in"].getString();
            command += " --o " + outputname + fn_run;
            outputNodes = getOutputNodesRefine(outputname + "run", -1, 1, 3, nr_bodies);
            command += " --solvent_correct_fsc --multibody_masks " + joboptions["fn_bodies"].getString();

            inputNodes.emplace_back(joboptions["fn_in"].getString(), joboptions["fn_in"].node_type);

            // Sampling
            int iover = 1;
            command += " --oversampling " + floatToString((float)iover);
            int sampling = JobOption::getHealPixOrder(joboptions["sampling"].getString());
            if (sampling <= 0)
            throw "Wrong choice for sampling";

            // The sampling given in the GUI will be the oversampled one!
            command += " --healpix_order " + integerToString(sampling - iover);
            // Always perform local searches!
            command += " --auto_local_healpix_order " + integerToString(sampling - iover);

            // Offset range
            command += " --offset_range " + joboptions["offset_range"].getString();
            // The sampling given in the GUI will be the oversampled one!
            command += " --offset_step " + floatToString(joboptions["offset_step"].getNumber() * pow(2.0, iover));  // May throw

        }

        if (joboptions["do_subtracted_bodies"].getBoolean())
            command += " --reconstruct_subtracted_bodies ";

        // Always do compute stuff
        if (!joboptions["do_combine_thru_disc"].getBoolean())
            command += " --dont_combine_weights_via_disc";
        if (!joboptions["do_parallel_discio"].getBoolean())
            command += " --no_parallel_disc_io";
        if (joboptions["do_preread_images"].getBoolean())
            command += " --preread_images " ;
        else if (joboptions["scratch_dir"].getString() != "")
            command += " --scratch_dir " +  joboptions["scratch_dir"].getString();
        command += " --pool " + joboptions["nr_pool"].getString();
        command += joboptions["do_pad1"].getBoolean() ? " --pad 1 " : " --pad 2 ";

        if (joboptions["skip_gridding"].getBoolean())
            command += " --skip_gridding ";

        // Running stuff
        command += " --j " + joboptions["nr_threads"].getString();

        // GPU-stuff
        if (joboptions["use_gpu"].getBoolean()) {
            command += " --gpu \"" + joboptions["gpu_ids"].getString() + "\"";
        }

        // Other arguments
        command += " " + joboptions["other_args"].getString();

        commands.push_back(command);
    }

    if (joboptions["do_analyse"].getBoolean()) {
        command = "`which relion_flex_analyse`";

        // If we had performed relion_refine command, then fn_run would be set now
        // Otherwise, we have to search for _model.star files that do NOT have a _it??? specifier
        if (fn_run.empty()) {
            FileName fn_wildcard = outputname + "run*_model.star";
            vector<FileName> fns_model;
            vector<FileName> fns_ok;
            fn_wildcard.globFiles(fns_model);
            for (const FileName &fn_model : fns_model) {
                if (!fn_model.contains("_it"))
                    fns_ok.push_back(fn_model);
            }
            if (fns_ok.empty())
            throw errorMsg("cannot find appropriate model.star file in the output directory");

            if (fns_ok.size() > 1)
            throw errorMsg("there is more than one model.star file (without '_it' specifiers) in the output directory. Move all but one out of the way.");

            fn_run = fns_ok[0].beforeFirstOf("_model.star");
        } else {
            fn_run = outputname + fn_run;
        }

        // General I/O
        command += " --PCA_orient ";
        command += " --model " + fn_run + "_model.star";
        command += " --data " + fn_run + "_data.star";
        command += " --bodies " + joboptions["fn_bodies"].getString();
        command += " --o " + outputname + "analyse";

        // Eigenvector movie maps
        if (joboptions["nr_movies"].getNumber() > 0) {
            command += " --do_maps ";
            command += " --k " + joboptions["nr_movies"].getString();
        }

        // Selection
        if (joboptions["do_select"].getBoolean()) {

            float minval = joboptions["eigenval_min"].getNumber();
            float maxval = joboptions["eigenval_max"].getNumber();
            if (minval >= maxval)
            throw errorMsg("the maximum eigenvalue should be larger than the minimum one!");

            command += " --select_eigenvalue "     + joboptions["select_eigenval"].getString();
            command += " --select_eigenvalue_min " + joboptions["eigenval_min"].getString();
            command += " --select_eigenvalue_max " + joboptions["eigenval_max"].getString();

            // Add output node: selected particles star file
            FileName fnt = outputname + "analyse_eval" + integerToString(joboptions["select_eigenval"].getNumber(), 3) + "_select";
            int min = round(joboptions["eigenval_min"].getNumber());
            int max = round(joboptions["eigenval_max"].getNumber());
            if (min > -99998)
            fnt += "_min" + integerToString(min);
            if (max < +99998)
            fnt += "_max" + integerToString(max);
            fnt += ".star";
            outputNodes.emplace_back(fnt, Node::PART_DATA);

        }

        // PDF with histograms of the eigenvalues
        outputNodes.emplace_back(outputname + "analyse_logfile.pdf", Node::PDF_LOGFILE);

        commands.push_back(command);
    }

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseMaskcreateJob() {
    hidden_name = ".gui_maskcreate";

    joboptions["fn_in"] = JobOption("Input 3D map:", Node::REF3D, "", "MRC map files (*.mrc)", "Provide an input MRC map from which to start binarizing the map.");

    joboptions["lowpass_filter"] = JobOption("Lowpass filter map (A)", 15, 10, 100, 5, "Lowpass filter that will be applied to the input map, prior to binarization. To calculate solvent masks, a lowpass filter of 15-20A may work well.");
    joboptions["angpix"] = JobOption("Pixel size (A)", -1, 0.3, 5, 0.1, "Provide the pixel size of the input map in Angstroms to calculate the low-pass filter. This value is also used in the output image header.");

    joboptions["inimask_threshold"] = JobOption("Initial binarisation threshold:", 0.02, 0.0, 0.5, 0.01, "This threshold is used to make an initial binary mask from the average of the two unfiltered half-reconstructions. \
If you don't know what value to use, display one of the unfiltered half-maps in a 3D surface rendering viewer and find the lowest threshold that gives no noise peaks outside the reconstruction.");
    joboptions["extend_inimask"] = JobOption("Extend binary map this many pixels:", 3, 0, 20, 1, "The initial binary mask is extended this number of pixels in all directions." );
    joboptions["width_mask_edge"] = JobOption("Add a soft-edge of this many pixels:", 3, 0, 20, 1, "The extended binary mask is further extended with a raised-cosine soft edge of the specified width." );

    joboptions["do_helix"] = JobOption("Mask a 3D helix?", false, "Generate a mask for 3D helix which spans across Z axis of the box.");
    joboptions["helical_z_percentage"] = JobOption("Central Z length (%):", 30.0, 5.0, 80.0, 1.0, "Reconstructed helix suffers from inaccuracies of orientation searches. \
The central part of the box contains more reliable information compared to the top and bottom parts along Z axis. Set this value (%) to the central part length along Z axis divided by the box size. Values around 30% are commonly used but you may want to try different lengths.");
}

string RelionJob::getCommandsMaskcreateJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::MASKCREATE_NAME, job_counter);
    string command = "`which relion_mask_create`";

    // I/O
    const FileName fn_in = joboptions["fn_in"].getString();
    if (fn_in.empty())
    throw errorMsg("empty field for input STAR file...");

    command += " --i " + fn_in;
    inputNodes.emplace_back(fn_in, joboptions["fn_in"].node_type);

    command += " --o " + outputname + "mask.mrc";
    outputNodes.emplace_back(outputname + "mask.mrc", Node::MASK);

    if (joboptions["lowpass_filter"].getNumber() > 0)
    command += " --lowpass " + joboptions["lowpass_filter"].getString();
    if (joboptions["angpix"].getNumber() > 0)
    command += " --angpix " + joboptions["angpix"].getString();

    command += " --ini_threshold "   + joboptions["inimask_threshold"].getString();
    command += " --extend_inimask "  + joboptions["extend_inimask"].getString();
    command += " --width_soft_edge " + joboptions["width_mask_edge"].getString();

    if (joboptions["do_helix"].getBoolean())
    command += " --helix --z_percentage " + floatToString(joboptions["helical_z_percentage"].getNumber() / 100.0);

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseJoinstarJob() {
    hidden_name = ".gui_joinstar";

    joboptions["do_part"] = JobOption("Combine particle STAR files?", false, "");
    joboptions["fn_part1"] = JobOption("Particle STAR file 1: ", Node::PART_DATA, "", "particle STAR file (*.star)", "The first of the particle STAR files to be combined.");
    joboptions["fn_part2"] = JobOption("Particle STAR file 2: ", Node::PART_DATA, "", "particle STAR file (*.star)", "The second of the particle STAR files to be combined.");
    joboptions["fn_part3"] = JobOption("Particle STAR file 3: ", Node::PART_DATA, "", "particle STAR file (*.star)", "The third of the particle STAR files to be combined. Leave empty if there are only two files to be combined.");
    joboptions["fn_part4"] = JobOption("Particle STAR file 4: ", Node::PART_DATA, "", "particle STAR file (*.star)", "The fourth of the particle STAR files to be combined. Leave empty if there are only two or three files to be combined.");

    joboptions["do_mic"] = JobOption("Combine micrograph STAR files?", false, "");
    joboptions["fn_mic1"] = JobOption("Micrograph STAR file 1: ", Node::MICS, "", "micrograph STAR file (*.star)", "The first of the micrograph STAR files to be combined.");
    joboptions["fn_mic2"] = JobOption("Micrograph STAR file 2: ", Node::MICS, "", "micrograph STAR file (*.star)", "The second of the micrograph STAR files to be combined.");
    joboptions["fn_mic3"] = JobOption("Micrograph STAR file 3: ", Node::MICS, "", "micrograph STAR file (*.star)", "The third of the micrograph STAR files to be combined. Leave empty if there are only two files to be combined.");
    joboptions["fn_mic4"] = JobOption("Micrograph STAR file 4: ", Node::MICS, "", "micrograph STAR file (*.star)", "The fourth of the micrograph STAR files to be combined. Leave empty if there are only two or three files to be combined.");

    joboptions["do_mov"] = JobOption("Combine movie STAR files?", false, "");
    joboptions["fn_mov1"] = JobOption("Movie STAR file 1: ", Node::MOVIES, "", "movie STAR file (*.star)", "The first of the micrograph movie STAR files to be combined.");
    joboptions["fn_mov2"] = JobOption("Movie STAR file 2: ", Node::MOVIES, "", "movie STAR file (*.star)", "The second of the micrograph movie STAR files to be combined.");
    joboptions["fn_mov3"] = JobOption("Movie STAR file 3: ", Node::MOVIES, "", "movie STAR file (*.star)", "The third of the micrograph movie STAR files to be combined. Leave empty if there are only two files to be combined.");
    joboptions["fn_mov4"] = JobOption("Movie STAR file 4: ", Node::MOVIES, "", "movie STAR file (*.star)", "The fourth of the micrograph movie STAR files to be combined. Leave empty if there are only two or three files to be combined.");
}

string RelionJob::getCommandsJoinstarJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::JOINSTAR_NAME, job_counter);
    string command = "`which relion_star_handler`";

    const int ii
        = joboptions["do_part"].getBoolean()
        + joboptions["do_mic"].getBoolean()
        + joboptions["do_mov"].getBoolean();
    if (ii == 0)
    throw "You've selected no type of files for joining. Select a single type!";
    if (ii > 1)
    throw "You've selected more than one type of files for joining. Only select a single type!";

    // I/O
    if (joboptions["do_part"].getBoolean()) {
        const FileName fn_part1 = joboptions["fn_part1"].getString();
        const FileName fn_part2 = joboptions["fn_part2"].getString();
        const FileName fn_part3 = joboptions["fn_part3"].getString();
        const FileName fn_part4 = joboptions["fn_part4"].getString();
        if (fn_part1.empty() || fn_part2.empty())
            throw errorMsg("empty field for first or second input STAR file...");

        command += " --combine --i \" " + fn_part1;
        inputNodes.emplace_back(fn_part1, joboptions["fn_part1"].node_type);
        command += " " + fn_part2;
        inputNodes.emplace_back(fn_part2, joboptions["fn_part2"].node_type);
        if (!fn_part3.empty()) {
            command += " " + fn_part3;
            inputNodes.emplace_back(fn_part3, joboptions["fn_part3"].node_type);
        }
        if (!fn_part4.empty()) {
            command += " " + fn_part4;
            inputNodes.emplace_back(fn_part4, joboptions["fn_part4"].node_type);
        }
        command += " \" ";

        // Check for duplicates
        command += " --check_duplicates rlnImageName ";
        command += " --o " + outputname + "join_particles.star";
        outputNodes.emplace_back(outputname + "join_particles.star", joboptions["fn_part1"].node_type);

    } else if (joboptions["do_mic"].getBoolean()) {
        const FileName fn_mic1 = joboptions["fn_mic1"].getString();
        const FileName fn_mic2 = joboptions["fn_mic2"].getString();
        const FileName fn_mic3 = joboptions["fn_mic3"].getString();
        const FileName fn_mic4 = joboptions["fn_mic4"].getString();
        if (fn_mic1.empty() || fn_mic2.empty())
            throw errorMsg("empty field for first or second input STAR file...");

        command += " --combine --i \" " + fn_mic1;
        inputNodes.emplace_back(fn_mic1, joboptions["fn_mic1"].node_type);
        command += " " + fn_mic2;
        inputNodes.emplace_back(fn_mic2, joboptions["fn_mic2"].node_type);
        if (!fn_mic3.empty()) {
            command += " " + fn_mic3;
            inputNodes.emplace_back(fn_mic3, joboptions["fn_mic3"].node_type);
        }
        if (!fn_mic4.empty()) {
            command += " " + fn_mic4;
            inputNodes.emplace_back(fn_mic4, joboptions["fn_mic4"].node_type);
        }
        command += " \" ";

        // Check for duplicates
        command += " --check_duplicates rlnMicrographName ";
        command += " --o " + outputname + "join_mics.star";
        outputNodes.emplace_back(outputname + "join_mics.star", joboptions["fn_mic1"].node_type);

    } else if (joboptions["do_mov"].getBoolean()) {
        const FileName fn_mov1 = joboptions["fn_mov1"].getString();
        const FileName fn_mov2 = joboptions["fn_mov2"].getString();
        const FileName fn_mov3 = joboptions["fn_mov3"].getString();
        const FileName fn_mov4 = joboptions["fn_mov4"].getString();
        if (fn_mov1.empty() || fn_mov2.empty())
            throw errorMsg("empty field for first or second input STAR file...");

        command += " --combine --i \" " + fn_mov1;
        inputNodes.emplace_back(fn_mov1, joboptions["fn_mov1"].node_type);
        command += " " + fn_mov2;
        inputNodes.emplace_back(fn_mov2, joboptions["fn_mov2"].node_type);
        if (!fn_mov3.empty()) {
            command += " " + fn_mov3;
            inputNodes.emplace_back(fn_mov3, joboptions["fn_mov3"].node_type);
        }
        if (!fn_mov4.empty()) {
            command += " " + fn_mov4;
            inputNodes.emplace_back(fn_mov4, joboptions["fn_mov4"].node_type);
        }
        command += " \" ";

        // Check for duplicates
        command += " --check_duplicates rlnMicrographMovieName ";
        command += " --o " + outputname + "join_movies.star";
        outputNodes.emplace_back(outputname + "join_movies.star", joboptions["fn_mov1"].node_type);
    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseSubtractJob() {
    hidden_name = ".gui_subtract";

    joboptions["fn_opt"] = JobOption("Input optimiser.star: ", string(""), "STAR Files (*_optimiser.star)", "./", "Select the *_optimiser.star file for the iteration of the 3D refinement/classification \
which you want to use for subtraction. It will use the maps from this run for the subtraction, and of no particles input STAR file is given below, it will use all of the particles from this run.");
    joboptions["fn_mask"] = JobOption("Mask of the signal to keep:", Node::MASK, "", "Image Files (*.{spi,vol,msk,mrc})", "Provide a soft mask where the protein density you wish to subtract from the experimental particles is black (0) and the density you wish to keep is white (1).");
    joboptions["do_data"] = JobOption("Use different particles?", false, "If set to Yes, subtraction will be performed on the particles in the STAR file below, instead of on all the particles of the 3D refinement/classification from the optimiser.star file.");
    joboptions["fn_data"] = JobOption("Input particle star file:", Node::PART_DATA, "", "particle STAR file (*.star)", "The particle STAR files with particles that will be used in the subtraction. Leave this field empty if all particles from the input refinement/classification run are to be used.");

    joboptions["do_fliplabel"] = JobOption("OR revert to original particles?", false, "If set to Yes, no signal subtraction is performed. Instead, the labels of rlnImageName and rlnImageOriginalName are flipped in the input STAR file given in the field below. This will make the STAR file point back to the original, non-subtracted images.");
    joboptions["fn_fliplabel"] = JobOption("revert this particle star file:", Node::PART_DATA, "", "particle STAR file (*.star)", "The particle STAR files with particles that will be used for label reversion.");

    joboptions["do_center_mask"] = JobOption("Do center subtracted images on mask?", true, "If set to Yes, the subtracted particles will be centered on projections of the center-of-mass of the input mask.");
    joboptions["do_center_xyz"] = JobOption("Do center on my coordinates?", false, "If set to Yes, the subtracted particles will be centered on projections of the x,y,z coordinates below. The unit is pixel, not angstrom. The origin is at the center of the box, not at the corner.");
    joboptions["center_x"] = JobOption("Center coordinate (pix) - X:", string("0"), "X-coordinate of the 3D center (in pixels).");
    joboptions["center_y"] = JobOption("Center coordinate (pix) - Y:", string("0"), "Y-coordinate of the 3D center (in pixels).");
    joboptions["center_z"] = JobOption("Center coordinate (pix) - Z:", string("0"), "Z-coordinate of the 3D center (in pixels).");

    joboptions["new_box"] = JobOption("New box size:", -1, 64, 512, 32, "Provide a non-negative value to re-window the subtracted particles in a smaller box size." );
}

string RelionJob::getCommandsSubtractJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::SUBTRACT_NAME, job_counter);
    string command;

    if (joboptions["do_fliplabel"].getBoolean()) {

        if (joboptions["nr_mpi"].getNumber() > 1)  // May throw
        throw "You cannot use MPI parallelization to revert particle labels.";

        inputNodes.emplace_back(joboptions["fn_fliplabel"].getString(), joboptions["fn_fliplabel"].node_type);

        outputNodes.emplace_back(outputname + "original.star", Node::PART_DATA);

        command = "`which relion_particle_subtract`";
        command += " --revert " + joboptions["fn_fliplabel"].getString() + " --o " + outputname;
    } else {
        command = joboptions["nr_mpi"].getNumber() > 1 ?
            "`which relion_particle_subtract_mpi`" : "`which relion_particle_subtract`";

        // I/O
        const FileName fn_opt = joboptions["fn_opt"].getString();
        if (fn_opt.empty())
        throw errorMsg("empty field for input optimiser.star...");

        command += " --i " + fn_opt;
        inputNodes.emplace_back(fn_opt, Node::OPTIMISER);

        const FileName fn_mask = joboptions["fn_mask"].getString();
        if (!fn_mask.empty()) {
            command += " --mask " + fn_mask;
            inputNodes.emplace_back(fn_mask, joboptions["fn_mask"].node_type);
        }
        const FileName fn_data = joboptions["fn_data"].getString();
        if (joboptions["do_data"].getBoolean()) {
            if (fn_data.empty())
            throw errorMsg("empty field for the input particle STAR file...");

            command += " --data " + fn_data;
            inputNodes.emplace_back(fn_data, joboptions["fn_data"].node_type);
        }

        command += " --o " + outputname;
        outputNodes.emplace_back(outputname + "particles_subtracted.star", Node::PART_DATA);

        if (joboptions["do_center_mask"].getBoolean()) {
            command += " --recenter_on_mask";
        } else if (joboptions["do_center_xyz"].getBoolean()) {
            command += " --center_x " + joboptions["center_x"].getString();
            command += " --center_y " + joboptions["center_y"].getString();
            command += " --center_z " + joboptions["center_z"].getString();
        }

        if (joboptions["new_box"].getNumber() > 0)
            command += " --new_box " + joboptions["new_box"].getString();

    }

    // Other arguments
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialisePostprocessJob() {
    hidden_name = ".gui_post";

    joboptions["fn_in"] = JobOption("One of the 2 unfiltered half-maps:", Node::HALFMAP, "", "MRC map files (*half1_*_unfil.mrc)",  "Provide one of the two unfiltered half-reconstructions that were output upon convergence of a 3D auto-refine run.");
    joboptions["fn_mask"] = JobOption("Solvent mask:", Node::MASK, "", "Image Files (*.{spi,vol,msk,mrc})", "Provide a soft mask where the protein is white (1) and the solvent is black (0). Often, the softer the mask the higher resolution estimates you will get. A soft edge of 5-10 pixels is often a good edge width.");
    joboptions["angpix"] = JobOption("Calibrated pixel size (A)", -1, 0.3, 5, 0.1, "Provide the final, calibrated pixel size in Angstroms. This value may be different from the pixel-size used thus far, e.g. when you have recalibrated the pixel size using the fit to a PDB model. The X-axis of the output FSC plot will use this calibrated value.");

    joboptions["do_auto_bfac"] = JobOption("Estimate B-factor automatically?", true, "If set to Yes, then the program will use the automated procedure described by Rosenthal and Henderson (2003, JMB) to estimate an overall B-factor for your map, and sharpen it accordingly. \
Note that your map must extend well beyond the lowest resolution included in the procedure below, which should not be set to resolutions much lower than 10 Angstroms. ");
    joboptions["autob_lowres"] = JobOption("Lowest resolution for auto-B fit (A):", 10, 8, 15, 0.5, "This is the lowest frequency (in Angstroms) that will be included in the linear fit of the Guinier plot as described in Rosenthal and Henderson (2003, JMB). Dont use values much lower or higher than 10 Angstroms. If your map does not extend beyond 10 Angstroms, then instead of the automated procedure use your own B-factor.");
    joboptions["do_adhoc_bfac"] = JobOption("Use your own B-factor?", false, "Instead of using the automated B-factor estimation, provide your own value. Use negative values for sharpening the map. \
This option is useful if your map does not extend beyond the 10A needed for the automated procedure, or when the automated procedure does not give a suitable value (e.g. in more disordered parts of the map).");
    joboptions["adhoc_bfac"] = JobOption("User-provided B-factor:", -1000, -2000, 0, -50, "Use negative values for sharpening. Be careful: if you over-sharpen your map, you may end up interpreting noise for signal!");

    joboptions["fn_mtf"] = JobOption("MTF of the detector (STAR file)", "", "STAR Files (*.star)", ".", "If you know the MTF of your detector, provide it here. Curves for some well-known detectors may be downloaded from the RELION Wiki. Also see there for the exact format \
\n If you do not know the MTF of your detector and do not want to measure it, then by leaving this entry empty, you include the MTF of your detector in your overall estimated B-factor upon sharpening the map.\
Although that is probably slightly less accurate, the overall quality of your map will probably not suffer very much.");
    joboptions["mtf_angpix"] = JobOption("Original detector pixel size:", 1.0, 0.3, 2.0, 0.1, "This is the original pixel size (in Angstroms) in the raw (non-super-resolution!) micrographs.");

    joboptions["do_skip_fsc_weighting"] = JobOption("Skip FSC-weighting?", false, "If set to No (the default), then the output map will be low-pass filtered according to the mask-corrected, gold-standard FSC-curve. \
Sometimes, it is also useful to provide an ad-hoc low-pass filter (option below), as due to local resolution variations some parts of the map may be better and other parts may be worse than the overall resolution as measured by the FSC. \
In such cases, set this option to Yes and provide an ad-hoc filter as described below.");
    joboptions["low_pass"] = JobOption("Ad-hoc low-pass filter (A):",5,1,40,1, "This option allows one to low-pass filter the map at a user-provided frequency (in Angstroms). When using a resolution that is higher than the gold-standard FSC-reported resolution, take care not to interpret noise in the map for signal...");
}

string RelionJob::getCommandsPostprocessJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::POST_NAME, job_counter);
    string command = "`which relion_postprocess`";

    // Input mask
    if (joboptions["fn_mask"].getString().empty())
    throw errorMsg("empty field for input mask...");

    command += " --mask " + joboptions["fn_mask"].getString();
    inputNodes.emplace_back(joboptions["fn_mask"].getString(), joboptions["fn_mask"].node_type);

    // Input half map (one of them)
    const FileName fn_half1 = joboptions["fn_in"].getString();
    if (fn_half1.empty())
    throw errorMsg("empty field for input half-map...");

    try {
        const FileName fn_half2 = getTheOtherHalf(fn_half1);
    } catch (const char *errmsg) {
        throw errorMsg(errmsg);
    }

    inputNodes.emplace_back(fn_half1, joboptions["fn_in"].node_type);
    command += " --i " + fn_half1;
    // The output name contains a directory: use it for output
    command += " --o " + outputname + "postprocess";
    command += "  --angpix " + joboptions["angpix"].getString();

    outputNodes.emplace_back(outputname + "postprocess.mrc",        Node::FINALMAP);
    outputNodes.emplace_back(outputname + "postprocess_masked.mrc", Node::FINALMAP);
    outputNodes.emplace_back(outputname + "logfile.pdf",            Node::PDF_LOGFILE);
    outputNodes.emplace_back(outputname + "postprocess.star",       Node::POST);

    // Sharpening
    if (joboptions["fn_mtf"].getString().length() > 0) {
        command += " --mtf " + joboptions["fn_mtf"].getString();
        command += " --mtf_angpix " + joboptions["mtf_angpix"].getString();
    }
    if (joboptions["do_auto_bfac"].getBoolean()) {
        command += " --auto_bfac ";
        command += " --autob_lowres " + joboptions["autob_lowres"].getString();
    }
    if (joboptions["do_adhoc_bfac"].getBoolean()) {
        command += " --adhoc_bfac " + joboptions["adhoc_bfac"].getString();
    }

    // Filtering
    if (joboptions["do_skip_fsc_weighting"].getBoolean()) {
        command += " --skip_fsc_weighting ";
        command += " --low_pass "  + joboptions["low_pass"].getString();
    }

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();

    commands.push_back(command);
    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseLocalresJob() {
    hidden_name = ".gui_localres";

    joboptions["fn_in"] = JobOption("One of the 2 unfiltered half-maps:", Node::HALFMAP, "", "MRC map files (*_unfil.mrc)",  "Provide one of the two unfiltered half-reconstructions that were output upon convergence of a 3D auto-refine run.");
    joboptions["angpix"] = JobOption("Calibrated pixel size (A)", 1, 0.3, 5, 0.1, "Provide the final, calibrated pixel size in Angstroms. This value may be different from the pixel-size used thus far, e.g. when you have recalibrated the pixel size using the fit to a PDB model. The X-axis of the output FSC plot will use this calibrated value.");

    // Check for environment variable RELION_RESMAP_EXECUTABLE
    const char *resmap_exe = getenv("RELION_RESMAP_EXECUTABLE");
    if (!resmap_exe) { resmap_exe = DEFAULT::RESMAPLOCATION; }

    joboptions["do_resmap_locres"] = JobOption("Use ResMap?", true, "If set to Yes, then ResMap will be used for local resolution estimation.");
    joboptions["fn_resmap"] = JobOption("ResMap executable:", string(resmap_exe), "ResMap*", ".", "Location of the ResMap executable. You can control the default of this field by setting environment variable RELION_RESMAP_EXECUTABLE, or by editing the first few lines in src/gui_jobwindow.h and recompile the code. \n \n Note that the ResMap wrapper cannot use MPI.");
    joboptions["fn_mask"] = JobOption("User-provided solvent mask:", Node::MASK, "", "Image Files (*.{spi,vol,msk,mrc})", "Provide a mask with values between 0 and 1 around all domains of the complex. ResMap uses this mask for local resolution calculation. RELION does NOT use this mask for calculation, but makes a histogram of local resolution within this mask.");
    joboptions["pval"] = JobOption("P-value:", 0.05, 0.0, 1.0, 0.01, "This value is typically left at 0.05. If you change it, report the modified value in your paper!");
    joboptions["minres"] = JobOption("Highest resolution (A): ", 0.0, 0.0, 10.0, 0.1, "ResMaps minRes parameter. By default (0), the program will start at just above 2x the pixel size");
    joboptions["maxres"] = JobOption("Lowest resolution (A): ", 0.0, 0.0, 10.0, 0.1, "ResMaps maxRes parameter. By default (0), the program will stop at 4x the pixel size");
    joboptions["stepres"] = JobOption("Resolution step size (A)", 1.0, 0.1, 3, 0.1, "ResMaps stepSize parameter." );

    joboptions["do_relion_locres"] = JobOption("Use Relion?", false, "If set to Yes, then relion_postprocess will be used for local-rtesolution estimation. This program basically performs a series of post-processing operations with a small soft, spherical mask that is moved over the entire map, while using phase-randomisation to estimate the convolution effects of that mask. \
\n \n The output relion_locres.mrc map can be used to color the surface of a map in UCSF Chimera according to its local resolution. The output relion_locres_filtered.mrc is a composite map that is locally filtered to the estimated resolution. \
This is a developmental feature in need of further testing, but initial results indicate it may be useful. \n \n Note that only this program can use MPI, the ResMap wrapper cannot use MPI.");

    //joboptions["locres_sampling"] = JobOption("Sampling rate (A):", 25, 5, 50, 5, "The local-resolution map will be calculated every so many Angstroms, by placing soft spherical masks on a cubic grid with this spacing. Very fine samplings (e.g. < 15A?) may take a long time to compute and give spurious estimates!");
    //joboptions["randomize_at"] = JobOption("Frequency for phase-randomisation (A): ", 10.0, 5, 20.0, 1, "From this frequency onwards, the phases for the mask-corrected FSC-calculation will be randomized. Make sure this is a lower resolution (i.e. a higher number) than the local resolutions you are after in your map.");
    joboptions["adhoc_bfac"] = JobOption("User-provided B-factor:", -100, -500, 0, -25, "Probably, the overall B-factor as was estimated in the postprocess is a useful value for here. Use negative values for sharpening. Be careful: if you over-sharpen your map, you may end up interpreting noise for signal!");
    joboptions["fn_mtf"] = JobOption("MTF of the detector (STAR file)", "", "STAR Files (*.star)", ".", "The MTF of the detector is used to complement the user-provided B-factor in the sharpening. If you don't have this curve, you can leave this field empty.");
}

string RelionJob::getCommandsLocalresJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::RESMAP_NAME, job_counter);

    if (joboptions["do_resmap_locres"].getBoolean() == joboptions["do_relion_locres"].getBoolean())
    throw errorMsg("choose either ResMap or Relion for local resolution estimation");

    if (joboptions["fn_in"].getString().empty())
    throw errorMsg("empty field for input half-map...");

    // Get the two half-reconstruction names from the single one
    const FileName fn_half1 = joboptions["fn_in"].getString();
    FileName fn_half2;
    try {
        fn_half2 = getTheOtherHalf(fn_half1);
    } catch (const char *errmsg) {
        throw errorMsg(errmsg);
    }

    inputNodes.emplace_back(joboptions["fn_in"].getString(), joboptions["fn_in"].node_type);

    string command;
    const FileName fn_mask = joboptions["fn_mask"].getString();

    if (joboptions["do_resmap_locres"].getBoolean()) {

        // ResMap wrapper
        if (joboptions["fn_resmap"].getString().empty())
        throw errorMsg("please provide an executable for the ResMap program.");

        if (joboptions["fn_mask"].getString().empty())
        throw errorMsg("Please provide an input mask for ResMap local-resolution estimation.");

        if (joboptions["do_queue"].getBoolean())
        throw errorMsg("You cannot submit a ResMap job to the queue, as it needs user interaction.");

        if (joboptions["nr_mpi"].getNumber() > 1)
        throw "You cannot use more than 1 MPI processor for the ResMap wrapper...";

        // Make symbolic links to the half-maps in the output directory
        commands.push_back("ln -s ../../" + fn_half1 + " " + outputname + "half1.mrc");
        commands.push_back("ln -s ../../" + fn_half2 + " " + outputname + "half2.mrc");

        inputNodes.emplace_back(fn_mask, joboptions["fn_mask"].node_type);

        outputNodes.emplace_back(outputname + "half1_resmap.mrc", Node::RESMAP);

        command = joboptions["fn_resmap"].getString();
        command += " --maskVol=" + fn_mask;
        command += " --noguiSplit " + outputname + "half1.mrc " +  outputname + "half2.mrc";
        command += " --vxSize=" + joboptions["angpix"].getString();
        command += " --pVal=" + joboptions["pval"].getString();
        command += " --minRes=" + joboptions["minres"].getString();
        command += " --maxRes=" + joboptions["maxres"].getString();
        command += " --stepRes=" + joboptions["stepres"].getString();

    } else if (joboptions["do_relion_locres"].getBoolean()) {
        // Relion postprocessing

        command = joboptions["nr_mpi"].getNumber() > 1 ?
            "`which relion_postprocess_mpi`" : "`which relion_postprocess`";

        command += " --locres --i " + joboptions["fn_in"].getString();
        command += " --o " + outputname + "relion";
        command += " --angpix " + joboptions["angpix"].getString();
        //command += " --locres_sampling " + joboptions["locres_sampling"].getString();
        //command += " --locres_randomize_at " + joboptions["randomize_at"].getString();
        command += " --adhoc_bfac " + joboptions["adhoc_bfac"].getString();
        const FileName fn_mtf = joboptions["fn_mtf"].getString();
        if (!fn_mtf.empty())
        command += " --mtf " + fn_mtf;

        if (!fn_mask.empty()) {
            command += " --mask " + fn_mask;
            outputNodes.emplace_back(outputname + "histogram.pdf", Node::PDF_LOGFILE);
        }

        outputNodes.emplace_back(outputname + "relion_locres_filtered.mrc", Node::FINALMAP);
        outputNodes.emplace_back(outputname + "relion_locres.mrc",          Node::RESMAP);
    }

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseMotionrefineJob() {
    hidden_name = ".gui_bayespolish";

    // I/O
    joboptions["fn_mic"] = JobOption("Micrographs (from MotionCorr):", Node::MICS,  "", "STAR files (*.star)", "The input STAR file with the micrograph (and their movie metadata) from a MotionCorr job.");
    joboptions["fn_data"] = JobOption("Particles (from Refine3D or CtfRefine):", Node::PART_DATA,  "", "STAR files (*.star)", "The input STAR file with the metadata of all particles.");
    joboptions["fn_post"] = JobOption("Postprocess STAR file:", Node::POST,  "", "STAR files (postprocess.star)", "The STAR file generated by a PostProcess job. \
The mask used for this postprocessing will be applied to the unfiltered half-maps and should encompass the entire complex. The resulting FSC curve will be used for weighting the different frequencies.");

    // Frame range
    joboptions["first_frame"] = JobOption("First movie frame: ", 1.0, 1.0, 10.0, 1, "First movie frame to take into account in motion fit and combination step");
    joboptions["last_frame"] = JobOption("Last movie frame: ", -1.0, 5.0, 50.0, 1, "Last movie frame to take into account in motion fit and combination step. Values equal to or smaller than 0 mean 'use all frames'.");

    joboptions["extract_size"] = JobOption("Extraction size (pix in unbinned movie):", -1, 64, 1024, 8, "Size of the extracted particles in the unbinned original movie(in pixels). This should be an even number.");
    joboptions["rescale"] = JobOption("Re-scaled size (pixels): ", -1, 64, 1024, 8, "The re-scaled value needs to be an even number.");

    // Parameter optimisation
    joboptions["do_param_optim"] = JobOption("Train optimal parameters?", false, "If set to Yes, then relion_motion_refine will estimate optimal parameter values for the three sigma values above on a subset of the data (determined by the minimum number of particles to be used below).");
    joboptions["eval_frac"] = JobOption("Fraction of Fourier pixels for testing: ", 0.5, 0, 1.0, 0.01, "This fraction of Fourier pixels (at higher resolution) will be used for evaluation of the parameters (test set), whereas the rest (at lower resolution) will be used for parameter estimation itself (work set).");
    joboptions["optim_min_part"] = JobOption("Use this many particles: ", 10000, 5000, 50000, 1000, "Use at least this many particles for the meta-parameter optimisation. The more particles the more expensive in time and computer memory the calculation becomes, but the better the results may get.");

    // motion_fit
    joboptions["do_polish"] = JobOption("Perform particle polishing?", true, "If set to Yes, then relion_motion_refine will be run to estimate per-particle motion-tracks using the parameters below, and polished particles will be generated.");
    joboptions["opt_params"] = JobOption("Optimised parameter file:", Node::POLISH_PARAMS,  "", "TXT files (*.txt)", "The output TXT file from a previous Bayesian polishing job in which the optimal parameters were determined.");
    joboptions["do_own_params"] = JobOption("OR use your own parameters?", false, "If set to Yes, then the field for the optimised parameter file will be ignored and the parameters specified below will be used instead.");
    joboptions["sigma_vel"] = JobOption("Sigma for velocity (A/dose): ", 0.2, 1.0, 10.0, 0.1, "Standard deviation for the velocity regularisation. Smaller values requires the tracks to be shorter.");
    joboptions["sigma_div"] = JobOption("Sigma for divergence (A): ", 5000, 0, 10000, 10000, "Standard deviation for the divergence of tracks across the micrograph. Smaller values requires the tracks to be spatially more uniform in a micrograph.");
    joboptions["sigma_acc"] = JobOption("Sigma for acceleration (A/dose): ", 2, -1, 7, 0.1, "Standard deviation for the acceleration regularisation. Smaller values requires the tracks to be straighter.");

    //combine_frames
    joboptions["minres"] = JobOption("Minimum resolution for B-factor fit (A): ", 20, 8, 40, 1, "The minimum spatial frequency (in Angstrom) used in the B-factor fit.");
    joboptions["maxres"] = JobOption("Maximum resolution for B-factor fit (A): ", -1, -1, 15, 1, "The maximum spatial frequency (in Angstrom) used in the B-factor fit. If a negative value is given, the maximum is determined from the input FSC curve.");
}

string RelionJob::getCommandsMotionrefineJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::MOTIONREFINE_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?
        "`which relion_motion_refine_mpi`" : "`which relion_motion_refine`";

    if (joboptions["fn_data"].getString().empty())
    throw errorMsg("empty field for input particle STAR file...");

    if (joboptions["fn_mic"].getString().empty())
    throw errorMsg("empty field for input micrograph STAR file...");

    if (joboptions["fn_post"].getString().empty())
    throw errorMsg("empty field for input PostProcess STAR file...");

    if (joboptions["do_param_optim"].getBoolean() && joboptions["do_polish"].getBoolean())
    throw errorMsg("Choose either parameter training or polishing, not both.");

    if (!joboptions["do_param_optim"].getBoolean() && !joboptions["do_polish"].getBoolean())
    throw errorMsg("nothing to do, choose either parameter training or polishing.");

    if (!joboptions["eval_frac"].isSchedulerVariable() && (
        joboptions["eval_frac"].getNumber() <= 0.1 ||
        joboptions["eval_frac"].getNumber() >  0.9
    ))
    throw errorMsg("the fraction of Fourier pixels used for evaluation should be between 0.1 and 0.9.");

    const FileName fn_data = joboptions["fn_data"].getString();
    const FileName fn_post = joboptions["fn_post"].getString();
    inputNodes.emplace_back(fn_data, joboptions["fn_data"].node_type);
    inputNodes.emplace_back(fn_post, joboptions["fn_post"].node_type);

    command += " --i " + fn_data;
    command += " --f " + fn_post;
    command += " --corr_mic " + joboptions["fn_mic"].getString();
    command += " --first_frame " + joboptions["first_frame"].getString();
    command += " --last_frame " + joboptions["last_frame"].getString();
    command += " --o " + outputname;

    if (joboptions["do_param_optim"].getBoolean()) {
        // Estimate meta-parameters
        RFLOAT align_frac = 1.0 - joboptions["eval_frac"].getNumber();
        command += " --min_p " + joboptions["optim_min_part"].getString();
        command += " --eval_frac " + joboptions["eval_frac"].getString();
        command += " --align_frac " + floatToString(align_frac);
        command += joboptions["sigma_acc"].getNumber() < 0 ?
            " --params2 " : " --params3 ";

        outputNodes.emplace_back(outputname + "opt_params_all_groups.txt", Node::POLISH_PARAMS);
    } else if (joboptions["do_polish"].getBoolean()) {
        if (joboptions["do_own_params"].getBoolean()) {
            // User-specified Parameters
            command += " --s_vel " + joboptions["sigma_vel"].getString();
            command += " --s_div " + joboptions["sigma_div"].getString();
            command += " --s_acc " + joboptions["sigma_acc"].getString();
        } else {
            if (joboptions["opt_params"].getString().empty())
            throw errorMsg("Please specify an optimised parameter file OR choose 'use own paramaeters' and set three sigma values.");

            command += " --params_file " + joboptions["opt_params"].getString();
        }

        command += " --combine_frames";
        command += " --bfac_minfreq " + joboptions["minres"].getString();
        command += " --bfac_maxfreq " + joboptions["maxres"].getString();

        const int window = round(joboptions["extract_size"].getNumber());
        const int scale  = round(joboptions["rescale"].getNumber());

        if (window * scale <= 0)
        throw errorMsg("Please specify both the extraction box size and the downsampled size, or leave both the default (-1)");

        if (window > 0 && scale > 0) {
            if (window % 2 != 0)
            throw errorMsg("The extraction box size must be an even number");

            command += " --window " + joboptions["extract_size"].getString();

            if (scale % 2 != 0)
            throw errorMsg("The downsampled box size must be an even number.");

            if (scale > window)
            throw errorMsg("The downsampled box size cannot be larger than the extraction size.");

            command += " --scale " + joboptions["rescale"].getString();
        }

        outputNodes.emplace_back(outputname + "logfile.pdf", Node::PDF_LOGFILE);
        outputNodes.emplace_back(outputname + "shiny.star",  Node::PART_DATA);

    }

    // If this is a continue job, then only process unfinished micrographs
    if (is_continue)
        command += " --only_do_unfinished ";

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseCtfrefineJob() {
    hidden_name = ".gui_ctfrefine";

    // I/O
    joboptions["fn_data"] = JobOption("Particles (from Refine3D):", Node::PART_DATA,  "", "STAR files (*.star)", "The input STAR file with the metadata of all particles.");
    joboptions["fn_post"] = JobOption("Postprocess STAR file:", Node::POST,  "", "STAR files (postprocess.star)", "The STAR file generated by a PostProcess job. \
The mask used for this postprocessing will be applied to the unfiltered half-maps and should encompass the entire complex. The resulting FSC curve will be used for weighting the different frequencies. \n \n Note that for helices it is common practice to use a mask only encompassing the central 30% or so of the box. \
This gives higher resolution estimates, as it disregards ill-defined regions near the box edges. However, for ctf_refine it is better to use a mask encompassing (almost) the entire box, as otherwise there may not be enough signal.");

    joboptions["minres"] = JobOption("Minimum resolution for fits (A): ", 30, 8, 40, 1, "The minimum spatial frequency (in Angstrom) used in the beamtilt fit.");

    // Defocus fit
    joboptions["do_ctf"] = JobOption("Perform CTF parameter fitting?", true, "If set to Yes, then relion_ctf_refine will be used to estimate the selected parameters below.");
    joboptions["do_defocus"] = JobOption("Fit defocus?", job_ctffit_options, 0, "If set to per-particle or per-micrograph, then relion_ctf_refine will estimate defocus values.");
    joboptions["do_astig"] = JobOption("Fit astigmatism?", job_ctffit_options, 0, "If set to per-particle or per-micrograph, then relion_ctf_refine will estimate astigmatism.");
    joboptions["do_bfactor"] = JobOption("Fit B-factor?", job_ctffit_options, 0, "If set to per-particle or per-micrograph, then relion_ctf_refine will estimate B-factors that describe the signal falloff.");
    joboptions["do_phase"] = JobOption("Fit phase-shift?", job_ctffit_options, 0, "If set to per-particle or per-micrograph, then relion_ctf_refine will estimate (VPP?) phase shift values.");

    // aberrations
    joboptions["do_aniso_mag"] = JobOption("Estimate (anisotropic) magnification?", false, "If set to Yes, then relion_ctf_refine will also estimate the (anisotropic) magnification per optics group. \
This option cannot be done simultaneously with higher-order aberration estimation. It's probably best to estimate the one that is most off first, and the other one second. It might be worth repeating the estimation if both are off.");

    joboptions["do_tilt"] = JobOption("Estimate beamtilt?", false, "If set to Yes, then relion_ctf_refine will also estimate the beamtilt per optics group. This option is only recommended for data sets that extend beyond 4.5 Angstrom resolution.");
    joboptions["do_trefoil"] = JobOption("Also estimate trefoil?", false, "If set to Yes, then relion_ctf_refine will also estimate the trefoil (3-fold astigmatism) per optics group. This option is only recommended for data sets that extend beyond 3.5 Angstrom resolution.");

    joboptions["do_4thorder"] = JobOption("Estimate 4th order aberrations?", false, "If set to Yes, then relion_ctf_refine will also estimate the Cs and the tetrafoil (4-fold astigmatism) per optics group. This option is only recommended for data sets that extend beyond 3 Angstrom resolution.");
}

string RelionJob::getCommandsCtfrefineJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::CTFREFINE_NAME, job_counter);
    string command = joboptions["nr_mpi"].getNumber() > 1 ?
        "`which relion_ctf_refine_mpi`" : "`which relion_ctf_refine`";

    const FileName fn_data = joboptions["fn_data"].getString();
    const FileName fn_post = joboptions["fn_post"].getString();
    if (fn_data.empty())
    throw errorMsg("empty field for input particle STAR file...");

    if (fn_post.empty())
    throw errorMsg("empty field for input PostProcess STAR file...");

    if (
        !joboptions["do_aniso_mag"].getBoolean() &&
        !joboptions["do_ctf"].getBoolean() &&
        !joboptions["do_tilt"].getBoolean() &&
        !joboptions["do_4thorder"].getBoolean()
    )
    throw errorMsg("you haven't selected to fit anything...");

    if (
        !joboptions["do_aniso_mag"].getBoolean() &&
        joboptions["do_ctf"].getBoolean() &&
        joboptions["do_defocus"].getString() == job_ctffit_options[0] &&
        joboptions["do_astig"].getString() == job_ctffit_options[0] &&
        joboptions["do_bfactor"].getString() == job_ctffit_options[0] &&
        joboptions["do_phase"].getString() == job_ctffit_options[0]
    )
    throw errorMsg("you did not select any CTF parameter to fit. Either switch off CTF parameter fitting, or select one to fit.");

    inputNodes.emplace_back(fn_data, joboptions["fn_data"].node_type);
    inputNodes.emplace_back(fn_post, joboptions["fn_post"].node_type);

    outputNodes.emplace_back(outputname + "logfile.pdf", Node::PDF_LOGFILE);

    command += " --i " + fn_data;
    command += " --f " + fn_post;
    command += " --o " + outputname;

    // Always either do anisotropic magnification, or CTF,tilt-odd,even
    if (joboptions["do_aniso_mag"].getBoolean()) {
        command += " --fit_aniso";
        command += " --kmin_mag " + joboptions["minres"].getString();
    } else {
        if (joboptions["do_ctf"].getBoolean()) {
            command += " --fit_defocus --kmin_defocus " + joboptions["minres"].getString();
            const string fit_options
                = JobOption::getCtfFitString(joboptions["do_phase"].getString())
                + JobOption::getCtfFitString(joboptions["do_defocus"].getString())
                + JobOption::getCtfFitString(joboptions["do_astig"].getString())
                + "f" // always have Cs refinement switched off
                + JobOption::getCtfFitString(joboptions["do_bfactor"].getString());

            if (fit_options.size() != 5)
            throw "Wrong CTF fitting options";

            command += " --fit_mode " + fit_options;
        }

        // do not allow anisotropic magnification to be done simultaneously with higher-order aberrations
        if (joboptions["do_tilt"].getBoolean()) {
            command += " --fit_beamtilt";
            command += " --kmin_tilt " + joboptions["minres"].getString();
            if (joboptions["do_trefoil"].getBoolean()) {
                command += " --odd_aberr_max_n 3";
            }
        }
        if (joboptions["do_4thorder"].getBoolean()) {
            command += " --fit_aberr";
        }
    }

    // If this is a continue job, then only process unfinished micrographs
    if (is_continue) {
        command += " --only_do_unfinished ";
    }

    outputNodes.emplace_back(outputname + "particles_ctf_refine.star", Node::PART_DATA);

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}

void RelionJob::initialiseExternalJob() {
    hidden_name = ".gui_external";

    // I/O
    joboptions["fn_exe"] = JobOption("External executable:", "", "", ".", "Location of the script that will launch the external program. This script should write all its output in the directory specified with --o. Also, it should write in that same directory a file called RELION_JOB_EXIT_SUCCESS upon successful exit, and RELION_JOB_EXIT_FAILURE upon failure.");

    // Optional input nodes
    joboptions["in_mov"] = JobOption("Input movies: ", Node::MOVIES, "", "movie STAR file (*.star)", "Input movies. This will be passed with a --in_movies argument to the executable.");
    joboptions["in_mic"] = JobOption("Input micrographs: ", Node::MICS, "", "micrographs STAR file (*.star)", "Input micrographs. This will be passed with a --in_mics argument to the executable.");
    joboptions["in_part"] = JobOption("Input particles: ", Node::PART_DATA, "", "particles STAR file (*.star)", "Input particles. This will be passed with a --in_parts argument to the executable.");
    joboptions["in_coords"] = JobOption("Input coordinates:", Node::MIC_COORDS, "", "STAR files (coords_suffix*.star)", "Input coordinates. This will be passed with a --in_coords argument to the executable.");
    joboptions["in_3dref"] = JobOption("Input 3D reference: ", Node::REF3D, "", "MRC files (*.mrc)", "Input 3D reference map. This will be passed with a --in_3dref argument to the executable.");
    joboptions["in_mask"] = JobOption("Input 3D mask: ", Node::MASK, "", "MRC files (*.mrc)", "Input 3D mask. This will be passed with a --in_mask argument to the executable.");

    // Optional parameters
    for (int i = 1; i <= 10; i++) {
        const string i_str = std::to_string(i);
        joboptions["param" + i_str + "_label"] = JobOption(
            "Param" + i_str + " - label:", string(""),
            "Define label and value for optional parameters to the script."
            "These will be passed as an argument --label value"
        );
        // e.g. joboptions["param1_label"]
        joboptions["param" + i_str + "_value"] = JobOption(
            "Param" + i_str + " - value:" , string(""),
            "Define label and value for optional parameters to the script."
            "These will be passed as an argument --label value"
        );
        // e.g. joboptions["param1_value"]
    }
}

string RelionJob::getCommandsExternalJob(
    string &outputname, vector<string> &commands,
    bool do_makedir, int job_counter
) {
    commands.clear();
    initialisePipeline(outputname, Process::EXTERNAL_NAME, job_counter);

    if (joboptions["fn_exe"].getString().empty())
    throw errorMsg("empty field for the external executable script...");

    string command = joboptions["fn_exe"].getString();
    command += " --o " + outputname;

    // Optional input nodes
    const string in_mov = joboptions["in_mov"].getString();
    if (!in_mov.empty()) {
        inputNodes.emplace_back(in_mov, joboptions["in_mov"].node_type);
        command += " --in_movies " + in_mov;
    }
    const string in_mic = joboptions["in_mic"].getString();
    if (!in_mic.empty()) {
        inputNodes.emplace_back(in_mic, joboptions["in_mic"].node_type);
        command += " --in_mics " + in_mic;
    }
    const string in_part = joboptions["in_part"].getString();
    if (!in_part.empty()) {
        inputNodes.emplace_back(in_part, joboptions["in_part"].node_type);
        command += " --in_parts " + in_part;
    }
    const string in_coords = joboptions["in_coords"].getString();
    if (!in_coords.empty()) {
        inputNodes.emplace_back(in_coords, joboptions["in_coords"].node_type);
        command += " --in_coords " + in_coords;
    }
    const string in_3dref = joboptions["in_3dref"].getString();
    if (!in_3dref.empty()) {
        inputNodes.emplace_back(in_3dref, joboptions["in_3dref"].node_type);
        command += " --in_3dref " + in_3dref;
    }
    const string in_mask = joboptions["in_mask"].getString();
    if (!in_mask.empty()) {
        inputNodes.emplace_back(in_mask, joboptions["in_mask"].node_type);
        command += " --in_mask " + in_mask;
    }

    // Optional arguments
    for (int i = 1; i <= 10; i++) {
        const string i_str = std::to_string(i);
        const string label_string = joboptions["param" + i_str + "_label"].getString();
        // e.g. joboptions["param1_label"].getString()
        const string value_string = joboptions["param" + i_str + "_value"].getString();
        // e.g. joboptions["param1_value"].getString()
        if (!label_string.empty()) {
            command += " --" + label_string + " " + value_string;
        }
    }

    // Running stuff
    command += " --j " + joboptions["nr_threads"].getString();

    // Other arguments for extraction
    command += " " + joboptions["other_args"].getString();
    commands.push_back(command);

    return prepareFinalCommand(outputname, commands, do_makedir);
}
