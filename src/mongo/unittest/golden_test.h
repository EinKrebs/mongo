/**
 *    Copyright (C) 2022-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/filesystem.hpp>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include "mongo/base/string_data.h"
#include "mongo/unittest/temp_dir.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace mongo::unittest {

/**
 * Allows executing golden data tests. That is, tests that produce a text output which is compared
 * against checked-in expected results (a.k.a "golden data".)
 *
 * The test fails if its output doesn't match the golden file's contents, or if
 * the golden data file doesn't exist.
 * if expected output doesnt exist. When this happens, the actual and expected outputs will be
 * stored in the configured output location. This allows:
 *  - bulk comparison to determine if further code changes are needed or if the new results are
 * acceptable.
 *  - bulk update of expected outputs if the new outputs are acceptable.
 *
 * Usage:
 *     GoldenTestConfig myConfig("src/mongo/my_expected_output");
 *     TEST(MySuite, MyTest) {
 *         GoldenTestContext ctx(myConfig)
 *         ctx.outStream() << "print something here" << std::endl;
 *         ctx.outStream() << "print something else" << std::endl;
 *     }
 *
 * TODO: SERVER-63734, Replace with proper developer tooling to diff/update
 *
 * In order to diff the results, find the failed test(s) and run the diff command to show the
 * differences for tests that failed.
 *
 * Example:
 *     To obtain the expected and actual output folders:
 *     $> ninja -j400 +unittest_test | grep "^{" |\
 *        jq -s -c -r '.[] | select(.id == 6273501 ) | .attr.expectedOutputRoot + " "
 * +.attr.actualOutputRoot ' | sort | uniq
 *
 * you may need to adjust the command to work with your favorite diff tool.
 *
 * In order to accept the new test outputs as new "golden data", copy the actual outputs to golden
 * data folder.
 *
 * Example:
 *    To obtain the copy command:
 *    $> ninja -j400 +unittest_test | grep "^{" |\
 *       jq -s -c -r '.[] | select(.id == 6273501 ) | "cp -R " + .attr.actualOutputRoot + "/" + "*
 * ."' | sort | uniq
 */

/**
 * A configuration specific to each golden test suite.
 */
struct GoldenTestConfig {
    /**
     * A relative path to the golden data files. The path is relative to root of the repo.
     * This path can be shared by multiple suites.
     *
     * It is recommended to keep golden data is a separate subfolder from other source code files.
     * Good:
     *   src/mongo/unittest/expected_output
     *   src/mongo/my_module/my_sub_module/expected_output
     *
     * Bad:
     *   src/mongo/my_module/
     *   src
     *   ../..
     *   /etc
     *   C:\Windows
     */
    std::string relativePath;
};

/**
 * Global environment shared across all golden test suites.
 * Specifically, output directory is shared across all suites to allow simple directory diffing,
 * even if multiple suites were executed.
 */
class GoldenTestEnvironment {
private:
    GoldenTestEnvironment();

public:
    GoldenTestEnvironment(const GoldenTestEnvironment&) = delete;
    GoldenTestEnvironment& operator=(const GoldenTestEnvironment&) = delete;

    static GoldenTestEnvironment* getInstance();

    boost::filesystem::path actualOutputRoot() const {
        return _actualOutputRoot;
    }

    boost::filesystem::path expectedOutputRoot() const {
        return _expectedOutputRoot;
    }

    boost::filesystem::path goldenDataRoot() const {
        return _goldenDataRoot;
    }

private:
    boost::filesystem::path _goldenDataRoot;
    std::string _outputPathPrefix;
    boost::filesystem::path _actualOutputRoot;
    boost::filesystem::path _expectedOutputRoot;
};

/**
 * Context for each golden test that can be used to accumulate, verify and optionally overwrite test
 * output data. Format of the output data is left to the test implementation. However it is
 * recommended that the output: 1) Is in text format. 2) Can be udated incrementally. Incremental
 * changes to the production or test code should result in incremental changes to the test output.
 * This reduces the size the side of diffs and reduces chances of conflicts. 3) Includes both input
 * and output. This helps with inspecting the changes, without need to pattern match across files.
 */
class GoldenTestContext {
public:
    explicit GoldenTestContext(const GoldenTestConfig* config,
                               const TestInfo* testInfo = currentTestInfo(),
                               bool validateOnClose = true)
        : _env(GoldenTestEnvironment::getInstance()),
          _config(config),
          _testInfo(testInfo),
          _validateOnClose(validateOnClose) {}

    ~GoldenTestContext() noexcept(false) {
        if (_validateOnClose && !std::uncaught_exceptions()) {
            verifyOutput();
        }
    }

public:
    /**
     * Returns the output stream that a test should write its output to.
     * The output that is written here will be compared against expected golden data. If the output
     * that test produced differs from the expected output, the test will fail.
     */
    std::ostream& outStream() {
        return _outStream;
    }

    /**
     * Verifies that output accumulated in this context matches the expected output golden data.
     * If output does not match, the test fails with TestAssertionFailureException.
     *
     * Additionally, in case of mismatch:
     *  - a file with the actual test output is created.
     *  - a file with the expected output is created:
     *    this preserves the snapshot of the golden data that was used for verification, as the
     * files in the source repo can subsequently change. Output files are written to a temp files
     * location unless configured otherwise.
     */
    void verifyOutput();

    /**
     * Returns the path where the actual test output will be written.
     */
    boost::filesystem::path getActualOutputPath() const;

    /**
     * Returns the path where the expected test output will be written.
     */
    boost::filesystem::path getExpectedOutputPath() const;

    /**
     * Returns the path to the golden data used for verification.
     */
    boost::filesystem::path getGoldedDataPath() const;

    /**
     * Returns relative test path. Typically composed of suite and test names.
     */
    boost::filesystem::path getTestPath() const;

private:
    static const TestInfo* currentTestInfo() {
        return UnitTest::getInstance()->currentTestInfo();
    }

    void throwAssertionFailureException(const std::string& message);

    static std::string readFile(const boost::filesystem::path& path);
    static void writeFile(const boost::filesystem::path& path, const std::string& contents);

    static std::string sanitizeName(const std::string& str);
    static std::string toSnakeCase(const std::string& str);
    static boost::filesystem::path toTestPath(const std::string& suiteName,
                                              const std::string& testName);

    void failResultMismatch(const std::string& actualStr,
                            const boost::optional<std::string>& expectedStr,
                            const std::string& messsage);

private:
    GoldenTestEnvironment* _env;
    const GoldenTestConfig* _config;
    const TestInfo* _testInfo;
    bool _validateOnClose;
    std::ostringstream _outStream;
};

/**
 * Represents configuration variables used by golden tests.
 */
struct GoldenTestOptions {
    /**
     * Parses the options from environment variables that start with GOLDEN_TEST_ prefix.
     * Supported options:
     *  - GOLDEN_TEST_OUTPUT: (optional) specifies the "output" data member.
     */
    static GoldenTestOptions parseEnvironment();

    /**
     * Path that will be used to write expected and actual test outputs.
     * If not specified a temporary folder location will be used.
     */
    boost::optional<std::string> output;
};

}  // namespace mongo::unittest
