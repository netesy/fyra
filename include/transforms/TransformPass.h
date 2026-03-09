#pragma once

#include "ErrorReporter.h"
#include "ir/Function.h"
#include <memory>
#include <string>

namespace transforms {

/**
 * @brief Abstract base class for transformation passes with error reporting capabilities
 * 
 * This class provides a common interface for all transformation passes in the
 * Fyra compiler backend. It includes integrated error reporting, pass validation,
 * and metrics collection capabilities.
 */
class TransformPass {
public:
    /**
     * @brief Construct a new Transform Pass object
     * 
     * @param pass_name Name of the transformation pass
     * @param error_reporter Error reporter instance (optional, creates default if null)
     */
    explicit TransformPass(const std::string& pass_name, 
                          std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : pass_name_(pass_name), 
          error_reporter_(error_reporter ? error_reporter : std::make_shared<ErrorReporter>()),
          modified_ir_(false), execution_count_(0) {}

    /**
     * @brief Virtual destructor
     */
    virtual ~TransformPass() = default;

    /**
     * @brief Run the transformation pass on a function
     * 
     * This method orchestrates the complete pass execution including:
     * - Precondition validation
     * - Actual transformation
     * - Postcondition validation  
     * - Error reporting
     * 
     * @param func Function to transform
     * @return true if the pass modified the IR, false otherwise
     */
    bool run(ir::Function& func) {
        execution_count_++;
        modified_ir_ = false;
        
        // Clear previous run's state
        current_function_ = &func;
        
        // Report pass start (info level)
        reportInfo("Starting " + pass_name_ + " pass on function: " + func.getName());
        
        // Validate preconditions
        if (!validatePreconditions(func)) {
            reportCriticalError("Precondition validation failed for " + pass_name_);
            return false;
        }
        
        // Perform the actual transformation
        bool result = performTransformation(func);
        
        if (result) {
            modified_ir_ = true;
            
            // Validate postconditions if transformation claimed success
            if (!validatePostconditions(func)) {
                reportError("Postcondition validation failed for " + pass_name_);
                return false;
            }
            
            reportInfo(pass_name_ + " pass completed successfully, IR modified");
        } else {
            reportInfo(pass_name_ + " pass completed, no changes made");
        }
        
        return result;
    }

    /**
     * @brief Get the error reporter associated with this pass
     * 
     * @return std::shared_ptr<ErrorReporter> The error reporter
     */
    std::shared_ptr<ErrorReporter> getErrorReporter() const {
        return error_reporter_;
    }

    /**
     * @brief Get the name of this pass
     * 
     * @return const std::string& Pass name
     */
    const std::string& getPassName() const {
        return pass_name_;
    }

    /**
     * @brief Check if the last run modified the IR
     * 
     * @return true if IR was modified in the last run
     */
    bool hasModifiedIR() const {
        return modified_ir_;
    }

    /**
     * @brief Get the number of times this pass has been executed
     * 
     * @return size_t Execution count
     */
    size_t getExecutionCount() const {
        return execution_count_;
    }

protected:
    /**
     * @brief Pure virtual method to perform the actual transformation
     * 
     * Derived classes must implement this method to define their specific
     * transformation logic.
     * 
     * @param func Function to transform
     * @return true if the IR was modified, false otherwise
     */
    virtual bool performTransformation(ir::Function& func) = 0;

    /**
     * @brief Validate preconditions before running the pass
     * 
     * Default implementation returns true. Derived classes can override
     * to add specific validation logic.
     * 
     * @param func Function to validate
     * @return true if preconditions are met
     */
    virtual bool validatePreconditions(ir::Function& func) {
        (void)func;
        // Default: no specific preconditions
        return true;
    }

    /**
     * @brief Validate postconditions after running the pass
     * 
     * Default implementation returns true. Derived classes can override
     * to add specific validation logic.
     * 
     * @param func Function to validate
     * @return true if postconditions are met
     */
    virtual bool validatePostconditions(ir::Function& func) {
        (void)func;
        // Default: no specific postconditions
        return true;
    }

    // Error reporting convenience methods
    void reportError(const std::string& message, int line = -1,
                    int column = -1, const std::string& filename = "") {
        error_reporter_->reportError(message, line, column, filename);
    }

    void reportWarning(const std::string& message, int line = -1,
                      int column = -1, const std::string& filename = "") {
        error_reporter_->reportWarning(message, line, column, filename);
    }

    void reportCriticalError(const std::string& message, int line = -1,
                           int column = -1, const std::string& filename = "") {
        error_reporter_->reportCriticalError(message, line, column, filename);
    }

    void reportInfo(const std::string& message, int line = -1,
                   int column = -1, const std::string& filename = "") {
        error_reporter_->reportInfo(message, line, column, filename);
    }

    /**
     * @brief Get pointer to the currently processed function
     * 
     * @return ir::Function* Current function being processed
     */
    ir::Function* getCurrentFunction() const { 
        return current_function_;
    }

private:
    std::string pass_name_;                        ///< Name of this transformation pass
    std::shared_ptr<ErrorReporter> error_reporter_; ///< Error reporter instance
    bool modified_ir_;                             ///< Whether last run modified IR
    size_t execution_count_;                       ///< Number of times pass has been executed
    ir::Function* current_function_;               ///< Currently processed function
};

} // namespace transforms