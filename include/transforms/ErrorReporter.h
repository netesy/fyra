#pragma once

#include "PassError.h"
#include <vector>
#include <iostream>
#include <memory>

namespace transforms {

/**
 * @brief Manages error and warning collection and reporting for transformation passes
 * 
 * The ErrorReporter class provides a centralized mechanism for collecting,
 * categorizing, and reporting errors that occur during transformation passes.
 * It maintains separate counters for different severity levels and provides
 * methods for querying error state.
 */
class ErrorReporter {
public:
    /**
     * @brief Construct a new ErrorReporter object
     * 
     * @param output_stream Stream to write error messages (default: std::cerr)
     * @param verbose_mode Whether to output info messages (default: false)
     */
    explicit ErrorReporter(std::ostream& output_stream = std::cerr, bool verbose_mode = false)
        : output_stream_(output_stream), verbose_mode_(verbose_mode),
          info_count_(0), warning_count_(0), error_count_(0), critical_count_(0) {}

    /**
     * @brief Report an error to this reporter
     * 
     * @param error The PassError to report
     */
    void reportError(const PassError& error) {
        errors_.push_back(error);
        
        // Update counters
        switch (error.getSeverity()) {
            case ErrorSeverity::Info:
                info_count_++;
                break;
            case ErrorSeverity::Warning:
                warning_count_++;
                break;
            case ErrorSeverity::Error:
                error_count_++;
                break;
            case ErrorSeverity::Critical:
                critical_count_++;
                break;
        }

        // Output immediately based on severity and verbose mode
        if (error.getSeverity() != ErrorSeverity::Info || verbose_mode_) {
            output_stream_ << error.toString() << std::endl;
        }
    }

    /**
     * @brief Report a warning (convenience method)
     * 
     * @param message Warning message
     * @param line Source line (optional)
     * @param column Source column (optional)
     * @param filename Source filename (optional)
     */
    void reportWarning(const std::string& message, int line = -1, 
                      int column = -1, const std::string& filename = "") {
        reportError(PassError(line, column, filename, message, ErrorSeverity::Warning));
    }

    /**
     * @brief Report an error (convenience method)
     * 
     * @param message Error message
     * @param line Source line (optional)
     * @param column Source column (optional)
     * @param filename Source filename (optional)
     */
    void reportError(const std::string& message, int line = -1,
                    int column = -1, const std::string& filename = "") {
        reportError(PassError(line, column, filename, message, ErrorSeverity::Error));
    }

    /**
     * @brief Report a critical error (convenience method)
     * 
     * @param message Critical error message
     * @param line Source line (optional)
     * @param column Source column (optional)
     * @param filename Source filename (optional)
     */
    void reportCriticalError(const std::string& message, int line = -1,
                           int column = -1, const std::string& filename = "") {
        reportError(PassError(line, column, filename, message, ErrorSeverity::Critical));
    }

    /**
     * @brief Report an info message (convenience method)
     * 
     * @param message Info message
     * @param line Source line (optional)
     * @param column Source column (optional)
     * @param filename Source filename (optional)
     */
    void reportInfo(const std::string& message, int line = -1,
                   int column = -1, const std::string& filename = "") {
        reportError(PassError(line, column, filename, message, ErrorSeverity::Info));
    }

    // Query methods
    bool hasErrors() const { return error_count_ > 0 || critical_count_ > 0; }
    bool hasCriticalErrors() const { return critical_count_ > 0; }
    bool hasWarnings() const { return warning_count_ > 0; }

    // Counter getters
    size_t getInfoCount() const { return info_count_; }
    size_t getWarningCount() const { return warning_count_; }
    size_t getErrorCount() const { return error_count_; }
    size_t getCriticalCount() const { return critical_count_; }
    size_t getTotalErrorCount() const { return error_count_ + critical_count_; }

    /**
     * @brief Get all recorded errors/warnings
     * 
     * @return const std::vector<PassError>& Vector of all recorded errors
     */
    const std::vector<PassError>& getAllErrors() const { return errors_; }

    /**
     * @brief Clear all recorded errors and reset counters
     */
    void clear() {
        errors_.clear();
        info_count_ = warning_count_ = error_count_ = critical_count_ = 0;
    }

    /**
     * @brief Set verbose mode (whether to output info messages)
     * 
     * @param verbose True to enable verbose output
     */
    void setVerboseMode(bool verbose) { verbose_mode_ = verbose; }

    /**
     * @brief Print summary of all error counts
     */
    void printSummary() const {
        if (info_count_ > 0) {
            output_stream_ << "Info messages: " << info_count_ << std::endl;
        }
        if (warning_count_ > 0) {
            output_stream_ << "Warnings: " << warning_count_ << std::endl;
        }
        if (error_count_ > 0) {
            output_stream_ << "Errors: " << error_count_ << std::endl;
        }
        if (critical_count_ > 0) {
            output_stream_ << "Critical errors: " << critical_count_ << std::endl;
        }
        
        if (getTotalErrorCount() == 0 && warning_count_ == 0) {
            if (verbose_mode_ && info_count_ > 0) {
                output_stream_ << "Transformation completed successfully with " 
                             << info_count_ << " info messages." << std::endl;
            } else if (info_count_ == 0) {
                output_stream_ << "Transformation completed successfully." << std::endl;
            }
        }
    }

private:
    std::vector<PassError> errors_;     ///< All recorded errors/warnings
    std::ostream& output_stream_;       ///< Output stream for error messages
    bool verbose_mode_;                 ///< Whether to output info messages
    
    // Counters for different severity levels
    size_t info_count_;
    size_t warning_count_;
    size_t error_count_;
    size_t critical_count_;
};

} // namespace transforms