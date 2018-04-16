#include <TMC2130Stepper.h>
class EndstopDriver;

class StepperDriverBase {
public:
    StepperDriverBase(EndstopDriver* minES, EndstopDriver* maxES)
        : minEndstop(minES)
        , maxEndstop(maxES)
        , direction(true) {}
    virtual ~StepperDriverBase() {}
    inline EndstopDriver* getMinEndstop() { return minEndstop; }
    inline EndstopDriver* getMaxEndstop() { return maxEndstop; }
    /// Allows initialization of driver e.g. current, microsteps
    virtual bool init() { return true; }
    /// Executes the step if endstop is not triggered. Return tru eif endstop is triggered
    virtual bool stepCond() = 0;
    /// Always executes the step
    virtual void step() = 0;
    /// Set step signal low
    virtual void unstep() = 0;
    /// Set direction, true = max direction
    virtual void dir(bool d) = 0;
    /// Enable motor driver
    virtual void enable() = 0;
    /// Disable motor driver
    virtual void disable() = 0;
    // Return true if setting microsteps is supported
    virtual bool implementSetMicrosteps() { return false; }
    // Return true if setting current in software is supported
    virtual bool implementSetMaxCurrent() { return false; }
    /// Set microsteps. Must be a power of 2.
    virtual void setMicrosteps(uint16_t) {}
    /// Set max current as range 0..65536
    virtual void setMotorCurrent(uint16_t) {}
    /// Set current as percentage of maximum 0..100
    virtual void setMotorCurrentPercent(uint8_t) {}
    // Called before homing starts. Can be used e.g. to disable silent mode
    // or otherwise prepare for endstop detection.
    virtual void beforeHoming() {}
    virtual void afterHoming() {}
    virtual void status() {
        Com::printFLN(PSTR("not implemented"));
    }
    EndstopDriver* minEndstop;
    EndstopDriver* maxEndstop;
    bool direction;
    // uint32_t position;
};

/// Plain stepper driver with optional endstops attached.
template <class stepCls, class dirCls, class enableCls>
class SimpleStepperDriver : public StepperDriverBase {
public:
    SimpleStepperDriver(EndstopDriver* minES, EndstopDriver* maxES)
        : StepperDriverBase(minES, maxES) {}
    inline bool stepCond() final {
        if (direction) {
            if (!maxEndstop->update()) {
                stepCls::on();
                return false;
            }
        } else {
            if (!minEndstop->update()) {
                stepCls::on();
                return false;
            }
        }
        return true;
    }
    inline void step() final {
        stepCls::on();
    }
    inline void unstep() final {
        stepCls::off();
    }
    inline void dir(bool d) final {
        dirCls::set(d);
        direction = d;
    }
    inline void enable() final {
        enableCls::on();
    }
    inline void disable() final {
        enableCls::off();
    }
};

#define TRINAMIC_WAIT_RESOLUTION_uS 100
/// Wait for boolean 'condition' to become true until 'timeout' (in miliseconds)
#define WAIT_UNTIL(condition, timeout) \
    for(uint16_t count = 0; !condition || count < (timeout * 1000 / TRINAMIC_WAIT_RESOLUTION_uS); count++) { \
        HAL::delayMicroseconds(TRINAMIC_WAIT_RESOLUTION_uS); \
    }
/// Wait for driver standstill condition until timeout (in miliseconds)
#define TRINAMIC_WAIT_FOR_STANDSTILL(timeout) WAIT_UNTIL(driver.stst(), timeout)

/// TMC2130 stepper driver with SPI configuration.
template <class stepCls, class dirCls, class enableCls>
class TMC2130StepperDriver : public StepperDriverBase {
public:
    TMC2130StepperDriver(EndstopDriver* minES, EndstopDriver* maxES, uint16_t csPin)
        : StepperDriverBase(minES, maxES)
        , driver(TMC2130Stepper(csPin)) {}
    inline bool stepCond() final {
        if (direction) {
            if (!maxEndstop->update()) {
                stepCls::on();
                return false;
            }
        } else {
            if (!minEndstop->update()) {
                stepCls::on();
                return false;
            }
        }
        return true;
    }
    inline void step() final {
        stepCls::on();
    }
    inline void unstep() final {
        stepCls::off();
    }
    inline void dir(bool d) final {
        dirCls::set(d);
        direction = d;
    }
    inline void enable() final {
        enableCls::on();
    }
    inline void disable() final {
        enableCls::off();
    }
    inline bool init() final {
        Com::printF(PSTR("TMC2130 initialization..."));
        enableCls::off();
        driver.begin(); // Initiate pins and registers
        driver.test_connection();
        if (driver.test_connection() != 0) {
            Com::printFLN(PSTR("SPI error"));
            return false;
        }
        Com::printFLN(PSTR("chip version "), driver.version());
        TRINAMIC_WAIT_FOR_STANDSTILL(100) // Wait for driver stand-still
        driver.I_scale_analog(true);   // Set current reference source
        driver.interpolate(false);     // Set internal microstep interpolation
        driver.internal_Rsense(false); // External current sense resistor
        driver.sgt(0);                 // Netural Stallguard threshold
        driver.diag1_stall(true);      // DIAG1 pin as stall signal (endstop)
        enableCls::on();
        return true;
    }
    inline bool implementSetMaxCurrent() { return true; }

    inline void setMicrosteps(uint16_t microsteps) final {
        TRINAMIC_WAIT_FOR_STANDSTILL(100) // Wait for driver stand-still
        driver.microsteps(microsteps);
    }
    inline void setMotorCurrent(uint16_t current) final {
        TRINAMIC_WAIT_FOR_STANDSTILL(100) // Wait for driver stand-still
        driver.rms_current(current);
    }
    inline void status() final {
        Com::printFLN(PSTR("TMC2130 driver version "), driver.version());
        Com::printFLN(PSTR("\tConnection test "), driver.test_connection());
        Com::printFLN(PSTR("\tRMS current "), driver.rms_current());
        Com::printFLN(PSTR("\tMicrosteps "), driver.microsteps());
        Com::printFLN(PSTR("\tStallguard value "), driver.sg_result());
        Com::printFLN(PSTR("\tOver temperature "), driver.ot());
        Com::printFLN(PSTR("\tOver temperature prewarn "), driver.otpw());
    }

    inline void beforeHoming() {
        backup.GCONF = driver.GCONF();
        backup.CHOPCONF = driver.CHOPCONF();
        backup.COOLCONF = driver.COOLCONF();
        backup.PWMCONF = driver.PWMCONF();
        backup.TCOOLTHRS = driver.TCOOLTHRS();
        backup.TPWMTHRS = driver.TPWMTHRS();
    }

    inline void afterHoming() {
        driver.GCONF(backup.GCONF);
        driver.CHOPCONF(backup.CHOPCONF);
        driver.COOLCONF(backup.COOLCONF);
        driver.PWMCONF(backup.PWMCONF);
        driver.TCOOLTHRS(backup.TCOOLTHRS);
        driver.TPWMTHRS(backup.TPWMTHRS);
    }

private:
    TMC2130Stepper driver;
    struct {
        uint32_t GCONF;
        uint32_t CHOPCONF;
        uint32_t COOLCONF;
        uint32_t PWMCONF;
        uint32_t TCOOLTHRS;
        uint32_t TPWMTHRS;
    } backup;
};
