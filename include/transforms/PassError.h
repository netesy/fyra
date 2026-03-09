#pragma once

#include <string>
#include <sstream>

namespace transforms {

/**
 * @brief Enumeration of error severity levels for transformation passes
 */
enum class ErrorSeverity {
    Info,       // Informational messages and optimization statistics
    Warning,    // Suboptimal patterns detected, but compilation can continue
    Error,      // Optimization failed but IR remains valid
    Critical    // Pass cannot continue, invalid IR structure
};

/**
 * @brief Represents an error or warning that occurred during a transformation pass
 * 
 * This class encapsulates error information including source location tracking,
 * error messages, and severity levels for comprehensive error reporting in
 * transformation passes.
 */
class PassError {
public:
    /**
     * @brief Construct a new PassError object
     * 
     * @param line Source line number (0-based, -1 if unknown)
     * @param column Source column number (0-based, -1 if unknown)
     * @param filename Source filename (empty if unknown)
     * @param message Error message describing the issue
     * @param severity Severity level of the error
     */
    PassError(int line, int column, const std::string& filename,
              const std::string& message, ErrorSeverity severity)
        : line_(line), column_(column), filename_(filename),
          message_(message), severity_(severity) {}

    /**
     * @brief Construct a PassError without location information
     * 
     * @param message Error message
     * @param severity Severity level
     */
    PassError(const std::string& message, ErrorSeverity severity)
        : line_(-1), column_(-1), filename_(""), message_(message), severity_(severity) {}

    // Getters
    int getLine() const { return line_; }
    int getColumn() const { return column_; }
    const std::string& getFilename() const { return filename_; }
    const std::string& getMessage() const { return message_; }
    ErrorSeverity getSeverity() const { return severity_; }

    /**
     * @brief Convert error to human-readable string format
     * 
     * @return std::string Formatted error message
     */
    std::string toString() const {
        std::ostringstream oss;
        
        // Add severity prefix
        switch (severity_) {
            case ErrorSeverity::Info:
                oss << "INFO: ";
                break;
            case ErrorSeverity::Warning:
                oss << "WARNING: ";
                break;
            case ErrorSeverity::Error:
                oss << "ERROR: ";
                break;
            case ErrorSeverity::Critical:
                oss << "CRITICAL: ";
                break;
        }

        // Add location information if available
        if (!filename_.empty()) {
            oss << filename_;
            if (line_ >= 0) {
                oss << ":" << (line_ + 1); // Convert to 1-based for user display
                if (column_ >= 0) {
                    oss << ":" << (column_ + 1); // Convert to 1-based for user display
                }
            }
            oss << ": ";
        }

        oss << message_;
        return oss.str();
    }

    /**
     * @brief Check if this error should halt compilation
     * 
     * @return true if error is critical and compilation should stop
     */
    bool isCritical() const {
        return severity_ == ErrorSeverity::Critical;
    }

    /**
     * @brief Check if this is an error (not just warning/info)
     * 
     * @return true if this is an error or critical error
     */
    bool isError() const {
        return severity_ == ErrorSeverity::Error || severity_ == ErrorSeverity::Critical;
    }

private:
    int line_;              ///< Source line number (0-based, -1 if unknown)
    int column_;            ///< Source column number (0-based, -1 if unknown)
    std::string filename_;  ///< Source filename (empty if unknown)
    std::string message_;   ///< Error message
    ErrorSeverity severity_; ///< Error severity level
};

} // namespace transforms