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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/
/***************************************************************************
 *
 * Authors: J.R. Bilbao-Castro (jrbcast@ace.ual.es)
 *
 * Unidad de Bioinformatica of Centro Nacional de Biotecnologia, CSIC
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 *
 * All comments concerning this program package may be sent to the
 * e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#include <cmath>
#include <algorithm>
#include "src/strings.h"
#include "src/error.h"
#include "src/macros.h"
#include "src/gcc_version.h"

// For sscanf
#ifdef RELION_SINGLE_PRECISION
const char *double_pattern = "%f";
#else
const char *double_pattern = "%lf";
#endif

std::string removeChar(const std::string &str, char character) {
    std::string copy = str;
    copy.erase(std::remove(copy.begin(), copy.end(), character), copy.end());
    return copy;
}

std::string unescape(const std::string &str) {

    std::string copy;
    for (unsigned int i = 0; i < str.length(); i++) {
        char current_char = str[i];
         if (current_char == '\t') {
            // Turn tabs into spaces
            copy += ' ';
        } else if (
            // Omit these special characters
            current_char != '\n' && current_char != '\a' &&
            current_char != '\v' && current_char != '\b' &&
            current_char != '\r' && current_char != '\f'
        ) {
            copy += current_char;
        }
    }
    return copy;
}

std::string escapeStringForSTAR(const std::string &str) {
    // TODO: Currently this assumes that str does not contain new lines.
    if (str.empty()) return "\"\"";

    // Escape strings that either start with a quotation mark or contain whitespace.
    if (str.find_first_of("\"\'") !=  0 &&
        str.find_first_of(" \t")  == -1) return str;

    std::string escaped = "\"";
    escaped.reserve(str.length());

    for (auto it = str.begin(); it != str.end(); ++it) {
        if (*it == '\"') {
            const auto next = it + 1;
            // If this is the last character or the next character is whitespace,
            // append an alert character ('\a').
            if (next == str.end() || *next == ' ' || *next == '\t') escaped += "\a";
        }
        escaped += *it;
    }

    escaped += "\"";
    return escaped;
}

std::string simplify(const std::string &str) {
    std::string temp;

    // First, unescape string
    std::string straux = unescape(str);

    // Remove spaces from the beginning
    int pos = straux.find_first_not_of(' ');
    straux.erase(0, pos);

    // Trim the rest of spaces
    for (unsigned int i = 0; i < straux.length();) {
        temp += straux[i];

        if (straux[i] == ' ') {
            while (straux[i] == ' ') {
                i++;
            }
        } else {
            i++;
        }
    }

    // Remove space left at the end of the string
    // if needed
    if (temp.size() > 0 && temp[temp.size() - 1] == ' ') {
        temp.resize(temp.size() - 1);
    }

    return temp;
}

/** Trim all spaces from the begining and the end */
void trim(std::string &str) {
    std::string::size_type pos = str.find_last_not_of(' ');

    if (pos != std::string::npos) {
        str.erase(pos + 1);
        pos = str.find_first_not_of(' ');
        if (pos != std::string::npos)
            str.erase(0, pos);
    } else {
        str.clear();
    }
}

/* NOTE: not a very safe implemenation but standard c functions do not retrieve
 * more than 6 significative digits */
double textToDouble(const char *str, int _errno, std::string errmsg) {

    if (!str) REPORT_ERROR(errmsg);

    double retval;
    if (!sscanf(str, double_pattern, &retval)) {
        REPORT_ERROR(errmsg);
        return 0;
    }
    return retval;

}

float textToFloat(const char *str, int _errno, std::string errmsg) {

    if (!str) REPORT_ERROR(errmsg);
    float retval;
    if (!sscanf(str, "%f", &retval)) {
        REPORT_ERROR(errmsg);
        return 0;
    }
    return retval;

}

int textToInteger(const char *str, int _errno, std::string errmsg) {

    if (!str) REPORT_ERROR(errmsg);

    int retval;
    if (!sscanf(str, "%d", &retval)) {
        REPORT_ERROR(errmsg);
    }
    return retval;

}

bool textToBool(const char *str, int _errno, std::string errmsg) {

    if (!str) REPORT_ERROR(errmsg);

    if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0) {
        return true;
    } else if (strcasecmp(str, "false") == 0 || strcasecmp(str, "no")  == 0) {
        return false;
    } else {
        REPORT_ERROR(errmsg);
    }
}

long long textToLongLong(const char *str, int _errno, std::string errmsg) {

    if (!str) REPORT_ERROR(errmsg);

    long long int retval;
    if (!sscanf(str, "%lld", &retval)) {
        REPORT_ERROR(errmsg);
        return 0;
    }

    return retval;

}

// A return value of -1 means exponential format
int bestPrecision(float x, int width) {

    if (x == 0) return 1;

    // abs(x) = 10 ** exp
    const int exp = floor(log10(std::abs(x)));

    if (exp >= 0) {  // i.e. abs(x) >= 1
        if (exp > width - 3) {
            return -1;
        } else {
            const int prec = width - 2;
            return prec < 0 ? -1 : prec;
        }
    } else {  // i.e. abs(x) < 1
        const int prec = width + exp - 1 - 3;
        return prec <= 0 ? -1 : prec;
    }
}

bool isNumber(const std::string &str) {
    float floatval;
    return sscanf(str.c_str(), "%f", &floatval);
}

std::string floatToString(float F, int width, int prec) {
    #if GCC_VERSION < 30300
    char aux[15];
    std::ostrstream outs(aux, sizeof(aux));
    #else
    std::ostringstream outs;
    #endif

    outs.fill(' ');

    if (width != 0)
        outs.width(width);

    if (prec == 0)
        prec = bestPrecision(F, width);

    if (prec == -1 && width > 7) {
        outs.precision(width - 7);
        outs.setf(std::ios::scientific);
    } else {
        outs.precision(prec);
    }

    #if GCC_VERSION < 30301
    outs << F << std::ends;
    #else
    outs << F;
    #endif

    #if GCC_VERSION < 30300
    return std::string(aux);
    #else

    const std::string retval = outs.str();
    const int i = retval.find('\0');
    if (i == -1) return retval;
    return retval.substr(0, i);

    #endif
}

std::string integerToString(int I, int width, char fill_with) {

    // Check width
    int mag = abs(I);
    if (width == 0) {
        do {
            mag /= 10;
            width++;
        } while (mag != 0);
    } else if (I < 0) {
        width--;
    }

    // Fill with fill character
    char buffer[15] = {fill_with};

    buffer[width--] = '\0';  // Null-terminate

    // Write digits
    mag = abs(I);
    do {
        buffer[width--] = '0' + mag % 10;
        mag /= 10;
    } while (mag != 0);

    return static_cast<std::string>(I < 0 ? "-" : "") + buffer;
}

void checkAngle(const std::string &str) {
    if (str != "rot" && str != "tilt" && str != "psi")
    REPORT_ERROR(static_cast<std::string>("checkAngle: Unrecognized angle type: " + str));
}

std::string removeSpaces(const std::string &str) {
    const std::string whitespace = "\n \t";
    std::string output;
    const int first = str.find_first_not_of(whitespace),
               last = str.find_last_not_of(whitespace);
    bool after_blank = false;

    for (int i = first; i <= last; i++) {
        if (whitespace.find(str[i]) != std::string::npos) {
            // Only add whitespace after a previous whitespace character
            if (!after_blank) { output += str[i]; }
            after_blank = true;
        } else {
            output += str[i];
            after_blank = false;
        }
    }

    return output;
}

// Remove quotes ===========================================================
void removeQuotes(char **str) {
    std::string retval = *str;
    if (retval.empty()) return;
    if (retval.front() == '\"' || retval.front() == '\'')
        retval = retval.substr(1, retval.length() - 1);
    if (retval.back() == '\"' || retval.back() == '\'')
        retval = retval.substr(0, retval.length() - 1);
    free(*str);
    *str = strdup(retval.c_str());
}

// Split a string ==========================================================

std::vector<std::string> split(
    const std::string &input, const std::string &delimiter
) {
    if (input.empty() || delimiter.empty()) return {input};

    int delimiter_index = input.find(delimiter, 0);
    if (delimiter_index == std::string::npos) return {input};

    const int delimiter_size = delimiter.size();
    std::vector<int> positions;
    for (int progress = 0; progress <= delimiter_index;) {
        positions.push_back(delimiter_index);
        progress = delimiter_index;
        delimiter_index = input.find(delimiter, progress + delimiter_size);
    }

    std::vector<std::string> results;
    for (int i = 0; i <= positions.size(); i++) {
        int curr_position = positions[i];
        int prev_position = positions[i - 1];
        int offset = i == 0 ? 0 : prev_position + delimiter_size;
        results.push_back(input.substr(offset, curr_position - offset));
    }
    return results;
}

// To lower ================================================================

inline char lowercase(char c) {
    return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

void toLower(char *str) {
    for (int i = 0; str[i] != '\0'; ++i) { str[i] = lowercase(str[i]); }
}

void toLower(std::string &str) {
    for (char &c : str) { c = lowercase(c); }
}

// Next token ==============================================================
std::string nextToken(const std::string &str, int &i) {

    // Beyond the end
    if (i >= str.length()) return "";

    // Only blanks
    int j = str.find_first_not_of(" \t\n", i);
    if (j == -1) return "";

    std::string retval;
    // There is a token. Where is the end?
    int k = str.find_first_of(" \t\n", j + 1);
    if (k == -1) { k = str.length(); }
    retval = str.substr(j, k - j + 1); // TAKANORI: CHECKME: TODO: I think this is a bug...
    i = k + 1;
    return retval;
}

bool nextTokenInSTAR(const std::string &str, int &i, std::string &retval) {
    const int len = str.length();

    // Beyond the end
    if (i >= len) return false;

    // Only blanks
    int start = str.find_first_not_of(" \t\n", i);
    if (start == -1) return false;

    // Only comment
    // '#' within a token does NOT start a comment; so this implementation is OK
    if (str[start] == '#') return false;

    // We do not support multiline string blocks franked by semicolons.

    retval = "";

    if (str[start] == '\'' || str[start] == '"') {
        // quoted string
        char quote = str[start];
        start++;

        // Start is the next character of the quote
        // Thus, it is always safe to look back one character
        int pos = start;
        int end = -1;
        while (pos < len) {
            if (str[pos] == quote && str[pos - 1] != '\a') {
                // Found un-escaped quote
                const int next_pos = pos + 1;
                if (
                    next_pos == len || // End of the string
                    str[next_pos] == ' ' || str[next_pos] == '\t' || str[next_pos] == '\n' // OR end of the token
                ) {
                    end = pos - 1; // Don't include myself
                    break;
                }
            }

            if (str[pos] != '\a')
                retval += str[pos]; // This is not efficient but we hope we don't have many quoted strings...

            pos++;
        }

        if (end == -1)
            REPORT_ERROR("nextTokenForSTAR:: Could not find closing quote in a STAR file. i = " + integerToString(i) + " pos = " + integerToString(pos) + " line:\n" + str);

        i = pos + 1;
		// std::cout << "QUOTED string: " << str << std::endl;
    } else {
        // Non-quoted string; faster code path
        int end = str.find_first_of(" \t\n", start + 1);
        if (end == -1) { end = len; }

        retval = str.substr(start, end - start);
		// std::cout << "NON-QUOTED string: '" << retval << "' in '" << str << "' given_i= " << i << std::endl;
        i = end + 1;
    }

    return true;
}

// Get word ================================================================
char *firstWord(char *str, int _errno, const std::string &errmsg) {
    // Get token
    char *token = str ? firstToken(str) : nextToken();
    // Check that there is something
    if (!token) REPORT_ERROR(errmsg);
    return token;
}

// Tokenize a C++ string ===================================================
void tokenize(
    const std::string &str, std::vector<std::string> &tokens,
    const std::string &delimiters
) {
    tokens.clear();
    // Skip delimiters at beginning.
    std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    std::string::size_type pos = str.find_first_of(delimiters, lastPos);

    while (std::string::npos != pos || std::string::npos != lastPos) {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);
    }
}

std::string join(const std::vector<std::string> &v, const std::string &delim) {
    std::string result;
    std::vector<std::string>::const_iterator it = v.begin();
    for (; it != v.end() - 1; ++it) {
        result.append(*it);
        result.append(delim);
    }
    result.append(*it);
    return result;
}

std::string join(const std::vector<char> &v, const std::string &delim) {
    std::string result;
    std::vector<char>::const_iterator it = v.begin();
    for (; it != v.end() - 1; ++it) {
        result += *it;
        result.append(delim);
    }
    result += *it;
    return result;
}
