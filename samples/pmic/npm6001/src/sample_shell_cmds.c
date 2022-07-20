/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "drv_npm6001.h"

/**@brief List of register names (contents of nPM6001 NRF_DIGITAL_Type struct).
 * @details Used to enable tab-completion for register names.
 */
#define REGISTER_NAME_LIST \
	X(SWREADY)\
	X(TASKS_START_DCDC3)\
	X(TASKS_START_LDO0)\
	X(TASKS_START_LDO1)\
	X(TASKS_START_THWARN)\
	X(TASKS_START_TH_SHUTDN)\
	X(TASKS_STOP_DCDC3)\
	X(TASKS_STOP_LDO0)\
	X(TASKS_STOP_LDO1)\
	X(TASKS_STOP_THWARN)\
	X(TASKS_STOP_THSHUTDN)\
	X(TASKS_UPDATE_VOUTPWM)\
	X(EVENTS_THWARN)\
	X(EVENTS_DCDC0OC)\
	X(EVENTS_DCDC1OC)\
	X(EVENTS_DCDC2OC)\
	X(EVENTS_DCDC3OC)\
	X(INTEN0)\
	X(INTENSET0)\
	X(INTENCLR0)\
	X(INTPEND0)\
	X(DCDC0VOUTULP)\
	X(DCDC0VOUTPWM)\
	X(DCDC1VOUTULP)\
	X(DCDC1VOUTPWM)\
	X(DCDC2VOUTULP)\
	X(DCDC2VOUTPWM)\
	X(DCDC3SELDAC)\
	X(DCDC3VOUT)\
	X(LDO0VOUT)\
	X(DCDC0CONFPWMMODE)\
	X(DCDC1CONFPWMMODE)\
	X(DCDC2CONFPWMMODE)\
	X(DCDC3CONFPWMMODE)\
	X(DCDCMODEPADCONF)\
	X(THDYNPOWERUP)\
	X(PADDRIVESTRENGTH)\
	X(WDARMEDVALUE)\
	X(WDARMEDSTROBE)\
	X(WDTRIGGERVALUE0)\
	X(WDTRIGGERVALUE1)\
	X(WDTRIGGERVALUE2)\
	X(WDDATASTROBE)\
	X(WDPWRUPVALUE)\
	X(WDPWRUPSTROBE)\
	X(WDKICK)\
	X(WDREQPOWERDOWN)\
	X(GENIOOUTSET)\
	X(GENIOOUTCLR)\
	X(GENIOIN)\
	X(GENIO0CONF)\
	X(GENIO1CONF)\
	X(GENIO2CONF)\
	X(LDO0CTRL)\
	X(LDO1CTRL)\
	X(OVERRIDEPWRUPDCDC)

struct reg_name_addr_pair {
	const char *name;
	uint8_t addr;
};

static const struct reg_name_addr_pair registers[] = {
#define X(_reg_name) {.name = STRINGIFY(_reg_name), .addr = offsetof(NRF_DIGITAL_Type, _reg_name)},
	REGISTER_NAME_LIST
#undef X
};

static const struct device *i2c_dev = DEVICE_DT_GET_ONE(nordic_nrf_twim);

static uint8_t twi_read(const struct shell *sh, uint8_t reg_addr)
{
	uint8_t reg_val = 0;
	int err;

	err = i2c_write_read(i2c_dev, DRV_NPM6001_TWI_ADDR,
		&reg_addr, sizeof(reg_addr),
		&reg_val, sizeof(reg_val));

	if (err) {
		shell_error(sh, "TWI read error");
	}

	return reg_val;
}

static void twi_write(const struct shell *sh, uint8_t reg_addr, uint8_t reg_val)
{
	struct i2c_msg msgs[] = {
		{.buf = &reg_addr, .len = sizeof(reg_addr), .flags = I2C_MSG_WRITE},
		{.buf = &reg_val, .len = sizeof(reg_val), .flags = I2C_MSG_WRITE | I2C_MSG_STOP},
	};
	int err;

	err = i2c_transfer(i2c_dev, &msgs[0], ARRAY_SIZE(msgs), DRV_NPM6001_TWI_ADDR);
	if (err) {
		shell_error(sh, "TWI write error");
	}
}

static int cmd_reg_named(const struct shell *sh, size_t argc, char **argv)
{
	const struct reg_name_addr_pair *reg = NULL;
	uint8_t reg_val;

	if (argc == 0) {
		shell_error(sh, "Invalid argument count");
		return -ENOEXEC;
	}

	if (strcmp(argv[0], "reg") == 0) {
		shell_print(sh, "Please specify which register to read or write");
		shell_print(sh, "(hint: use 'tab' key for autocompletion)");
		return 0;
	}

	for (int i = 0; i < ARRAY_SIZE(registers); ++i) {
		if (strcmp(argv[0], registers[i].name) == 0) {
			reg = &registers[i];
			break;
		}
	}

	if (reg == NULL) {
		shell_warn(sh, "Register name not found");
		return 0;
	}

	if (argc > 1) {
		/* Write register */
		if (strncmp(argv[1], "0b", 2) == 0 && strlen(argv[1]) > 2) {
			/* Skip "0b" for base 2 */
			reg_val = (uint8_t) strtol(&argv[1][2], NULL, 2);
		} else {
			/* strtol figures out base */
			reg_val = (uint8_t) strtol(argv[1], NULL, 0);
		}

		shell_print(sh, "Writing 0x%02X to %s", reg_val, reg->name);
		twi_write(sh, reg->addr, reg_val);
	} else {
		/* Read register */
		shell_print(sh, "%s=0x%02X", reg->name, twi_read(sh, reg->addr));
	}

	return 0;
}

#define X(_reg_name) SHELL_CMD_ARG(_reg_name, NULL, "[Value]", cmd_reg_named, 1, 1),
SHELL_STATIC_SUBCMD_SET_CREATE(npm6001_reg_cmds,
	REGISTER_NAME_LIST
	SHELL_SUBCMD_SET_END
);
#undef X

static int cmd_vreg(const struct shell *sh, size_t argc, char **argv)
{
	enum drv_npm6001_vreg_mode mode = DRV_NPM6001_MODE_ULP;
	enum drv_npm6001_vreg regulator;
	uint16_t voltage = 0;
	uint16_t v_min;
	uint16_t v_max;
	bool set_mode = false;
	bool set_voltage = false;
	bool turn_on = false;
	bool turn_off = false;
	int err;

	if (argc == 1) {
		shell_print(sh, "Usage: ~$ npm6001 vreg REGULATOR [MODE] [VOLTAGE mV]");
		shell_print(sh, "example, set output voltage: ~$ npm6001 vreg DCDC3 2500");
		shell_print(sh, "example, turn off: ~$ npm6001 vreg DCDC3 off");
		shell_print(sh, "example, set PWM mode: ~$ npm6001 vreg DCDC2 pwm");
		return 0;
	}

	if (strcmp(argv[0], "DCDC0") == 0) {
		regulator = DRV_NPM6001_DCDC0;
	} else if (strcmp(argv[0], "DCDC1") == 0) {
		regulator = DRV_NPM6001_DCDC1;
	} else if (strcmp(argv[0], "DCDC2") == 0) {
		regulator = DRV_NPM6001_DCDC2;
	} else if (strcmp(argv[0], "DCDC3") == 0) {
		regulator = DRV_NPM6001_DCDC3;
	} else if (strcmp(argv[0], "LDO0") == 0) {
		regulator = DRV_NPM6001_LDO0;
	} else if (strcmp(argv[0], "LDO1") == 0) {
		regulator = DRV_NPM6001_LDO1;
	} else {
		shell_print(sh, "Please specify valid regulator to adjust");
		shell_print(sh, "(hint: use 'tab' key for autocompletion)");
		return 0;
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "ON") == 0 || strcmp(argv[i], "on") == 0) {
			turn_on = true;
			break;
		}
		if (strcmp(argv[i], "OFF") == 0 || strcmp(argv[i], "off") == 0) {
			turn_off = true;
			break;
		}
		if (strcmp(argv[i], "0") == 0) {
			/* Interpret 0 mV as shut off */
			turn_off = true;
			break;
		}

		if (strcmp(argv[i], "ULP") == 0 || strcmp(argv[i], "ulp") == 0) {
			set_mode = true;
			mode = DRV_NPM6001_MODE_ULP;
		} else if (strcmp(argv[i], "PWM") == 0 || strcmp(argv[i], "pwm") == 0) {
			set_mode = true;
			mode = DRV_NPM6001_MODE_PWM;
		} else {
			set_voltage = true;
			voltage = strtol(argv[i], NULL, 0);
			if (voltage == 0) {
				shell_warn(sh, "Unexpected parameter: %s", argv[i]);
				return 0;
			}
		}
	}

	if (set_voltage && voltage == 0) {
		turn_off = true;
		set_voltage = false;
	} else if (set_voltage) {
		turn_on = true;
	}

	if (turn_off) {
		switch (regulator) {
		case DRV_NPM6001_DCDC0:
		case DRV_NPM6001_DCDC1:
		case DRV_NPM6001_DCDC2:
			shell_warn(sh, "%s cannot be turned off", argv[0]);
			return 0;
		default:
			break;
		}
	}

	if (set_mode && (regulator == DRV_NPM6001_LDO0 || regulator == DRV_NPM6001_LDO1)) {
		shell_warn(sh, "LDO regulators does not have mode selection");
		set_mode = false;
	}

	if (set_voltage) {
		switch (regulator) {
		case DRV_NPM6001_DCDC0:
			v_min = DRV_NPM6001_DCDC0_MINV;
			v_max = DRV_NPM6001_DCDC0_MAXV;
			break;
		case DRV_NPM6001_DCDC1:
			v_min = DRV_NPM6001_DCDC1_MINV;
			v_max = DRV_NPM6001_DCDC1_MAXV;
			break;
		case DRV_NPM6001_DCDC2:
			v_min = DRV_NPM6001_DCDC2_MINV;
			v_max = DRV_NPM6001_DCDC2_MAXV;
			break;
		case DRV_NPM6001_DCDC3:
			v_min = DRV_NPM6001_DCDC3_MINV;
			v_max = DRV_NPM6001_DCDC3_MAXV;
			break;
		case DRV_NPM6001_LDO0:
			v_min = DRV_NPM6001_LDO0_MINV;
			v_max = DRV_NPM6001_LDO0_MAXV;
			break;
		case DRV_NPM6001_LDO1:
			v_min = DRV_NPM6001_LDO1_MINV;
			v_max = DRV_NPM6001_LDO1_MAXV;
			break;
		default:
			shell_error(sh, "Internal error");
			return 0;
		}

		if (voltage < v_min || voltage > v_max) {
			shell_warn(sh, "Invalid voltage selection. Valid range for %s: %d mV - %d mV",
				argv[0], v_min, v_max);
				return 0;
		}

		if (regulator == DRV_NPM6001_LDO1) {
			/* LDO1 does not have configurable voltage. Only on/off. */
			set_voltage = false;
		}
	}

	if (set_mode) {
		err = drv_npm6001_vreg_dcdc_mode_set(regulator, mode);
		if (err) {
			shell_warn(sh, "Failed to set mode");
			return 0;
		}

		shell_print(sh, "Successfully set %s mode", argv[0]);
	}

	if (set_voltage && regulator == DRV_NPM6001_DCDC3) {
		/* Should turn DCDC3 off while adjusting voltage to prevent overshoot */
		err = drv_npm6001_vreg_disable(regulator);
		if (err) {
			shell_warn(sh, "Failed to disable %s", argv[0]);
		}
	}

	if (set_voltage) {
		if (regulator == DRV_NPM6001_DCDC3) {
			/* Should turn DCDC3 off while adjusting voltage to prevent overshoot */
			err = drv_npm6001_vreg_disable(regulator);
			if (err) {
				shell_warn(sh, "Failed to disable %s", argv[0]);
			} else {
				turn_on = true;
			}
		}
		err = drv_npm6001_vreg_voltage_set(regulator, voltage);
		if (err == -EINVAL) {
			shell_warn(sh, "Invalid voltage selection");
			return 0;
		} else if (err) {
			shell_warn(sh, "Failed to set voltage");
		} else {
			shell_print(sh, "Successfully set %s voltage", argv[0]);
		}
	}

	if (turn_on) {
		err = drv_npm6001_vreg_enable(regulator);
		if (err) {
			shell_warn(sh, "Failed to enable %s", argv[0]);
		}
	} else if (turn_off) {
		err = drv_npm6001_vreg_disable(regulator);
		if (err == -EINVAL) {
			shell_warn(sh, "%s cannot be disabled", argv[0]);
		} else if (err) {
			shell_warn(sh, "Failed to disable %s", argv[0]);
		} else {
			shell_print(sh, "Successfully disabled %s", argv[0]);
		}
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(npm6001_vreg_cmds,
	SHELL_CMD_ARG(DCDC0, NULL, "[on/off] [ULP/PWM] [Voltage] (1800 - 3300 mV)", cmd_vreg, 1, 3),
	SHELL_CMD_ARG(DCDC1, NULL, "[on/off] [ULP/PWM] [Voltage] (700 - 1400 mV)", cmd_vreg, 1, 3),
	SHELL_CMD_ARG(DCDC2, NULL, "[on/off] [ULP/PWM] [Voltage] (1200 - 1400 mV)", cmd_vreg, 1, 3),
	SHELL_CMD_ARG(DCDC3, NULL, "[on/off] [ULP/PWM] [Voltage] (500 - 3300 mV)", cmd_vreg, 1, 3),
	SHELL_CMD_ARG(LDO0, NULL, "[on/off] [Voltage] (1800 - 3300 mV)", cmd_vreg, 1, 3),
	SHELL_CMD_ARG(LDO1, NULL, "[on/off]", cmd_vreg, 1, 3),
	SHELL_SUBCMD_SET_END
);

static int cmd_wd_enable(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t timeout;
	int err;

	if (argc != 2) {
		shell_print(sh, "Usage: ~$ npm6001 watchdog enable [timeout]");
		return 0;
	}

	timeout = strtol(argv[1], NULL, 0);

	err = drv_npm6001_watchdog_enable(timeout);
	if (err == -EINVAL) {
		shell_warn(sh, "Invalid watchdog timeout");
		return 0;
	} else if (err) {
		shell_warn(sh, "Failed to enable watchdog");
	} else {
		shell_print(sh, "Successfully enabled watchdog");
	}

	return 0;
}

static int cmd_wd_disable(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = drv_npm6001_watchdog_disable();
	if (err) {
		shell_warn(sh, "Failed to disable watchdog");
	} else {
		shell_print(sh, "Successfully disabled watchdog");
	}

	return 0;
}

static int cmd_wd_kick(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = drv_npm6001_watchdog_kick();
	if (err) {
		shell_warn(sh, "Failed to kick watchdog");
	} else {
		shell_print(sh, "Successfully kicked watchdog");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(npm6001_wd_cmds,
	SHELL_CMD_ARG(enable, NULL, "[TIMEOUT] (4 second ticks)", cmd_wd_enable, 2, 1),
	SHELL_CMD_ARG(disable, NULL, "", cmd_wd_disable, 1, 1),
	SHELL_CMD_ARG(kick, NULL, "", cmd_wd_kick, 1, 1),
	SHELL_SUBCMD_SET_END
);

static int cmd_int(const struct shell *sh, size_t argc, char **argv)
{
	enum drv_npm6001_int interrupt;
	bool enable;
	int err;

	if (argc < 2) {
		shell_print(sh, "Usage: ~$ npm6001 interrupt TYPE [enable/disable]");
		shell_print(sh, "example: ~$ npm6001 THWARN enable");
		shell_print(sh, "         ~$ npm6001 DCDC1OC disable");
		return 0;
	}

	if (strcmp(argv[0], "THWARN") == 0) {
		interrupt = DRV_NPM6001_INT_THERMAL_WARNING;
	} else if (strcmp(argv[0], "DCDC0OC") == 0) {
		interrupt = DRV_NPM6001_INT_DCDC0_OVERCURRENT;
	} else if (strcmp(argv[0], "DCDC1OC") == 0) {
		interrupt = DRV_NPM6001_INT_DCDC1_OVERCURRENT;
	} else if (strcmp(argv[0], "DCDC2OC") == 0) {
		interrupt = DRV_NPM6001_INT_DCDC2_OVERCURRENT;
	} else if (strcmp(argv[0], "DCDC3OC") == 0) {
		interrupt = DRV_NPM6001_INT_DCDC3_OVERCURRENT;
	} else {
		shell_print(sh, "Please specify valid interrupt to configure");
		shell_print(sh, "(hint: use 'tab' key for autocompletion)");
		return 0;
	}

	if (strcmp(argv[1], "ENABLE") == 0 || strcmp(argv[1], "enable") == 0) {
		enable = true;
	} else if (strcmp(argv[1], "DISABLE") == 0 || strcmp(argv[1], "disable") == 0) {
		enable = false;
	} else {
		shell_warn(sh, "Invalid option");
		return 0;
	}

	if (enable) {
		err = drv_npm6001_int_enable(interrupt);
	} else {
		err = drv_npm6001_int_disable(interrupt);
	}

	if (err) {
		shell_error(sh, "Failed to %s interrupt", enable ? "enable" : "disable");
	} else {
		shell_print(sh, "Successfully %s interrupt", enable ? "enabled" : "disabled");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(npm6001_int_cmds,
	SHELL_CMD_ARG(THWARN, NULL, "Thermal warning [enable/disable]", cmd_int, 1, 2),
	SHELL_CMD_ARG(DCDC0OC, NULL, "DCDC0 overcurrent [enable/disable]", cmd_int, 1, 2),
	SHELL_CMD_ARG(DCDC1OC, NULL, "DCDC1 overcurrent [enable/disable]", cmd_int, 1, 2),
	SHELL_CMD_ARG(DCDC2OC, NULL, "DCDC2 overcurrent [enable/disable]", cmd_int, 1, 2),
	SHELL_CMD_ARG(DCDC3OC, NULL, "DCDC3 overcurrent [enable/disable]", cmd_int, 1, 2),
	SHELL_SUBCMD_SET_END
);

static int cmd_hibernate(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t timeout;
	int err;

	if (argc != 2) {
		shell_print(sh, "Usage: ~$ npm6001 hibernate timeout");
		shell_print(sh, "example, 1 hour hibernation: ~$ npm6001 hibernate 900");
		return 0;
	}

	timeout = strtol(argv[1], NULL, 0);

	err = drv_npm6001_hibernate(timeout);
	if (err == -EINVAL) {
		shell_warn(sh, "Invalid hibernation wakeup duration");
		return 0;
	} else if (err) {
		shell_warn(sh, "Failed to enable hibernation mode");
	} else {
		shell_print(sh, "Successfully enabled hibernation mode");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(npm6001_cmds,
	SHELL_CMD_ARG(vreg, &npm6001_vreg_cmds, "Voltage regulator control", cmd_vreg, 2, 1),
	SHELL_CMD_ARG(reg, &npm6001_reg_cmds, "Register read/write", NULL, 1, 2),
	SHELL_CMD_ARG(
		watchdog, &npm6001_wd_cmds, "Watchdog enable/disable/kick", cmd_wd_enable, 1, 1),
	SHELL_CMD_ARG(hibernate, NULL, "Hibernate", cmd_hibernate, 1, 1),
	SHELL_CMD_ARG(interrupt, &npm6001_int_cmds, "Interrupt enable/disable", cmd_int, 2, 1),

	SHELL_SUBCMD_SET_END
);

static int cmd_npm6001(const struct shell *sh, size_t argc, char **argv)
{
	shell_help(sh);

	return 0;
}

SHELL_CMD_ARG_REGISTER(npm6001, &npm6001_cmds, "nPM6001 shell commands",
			   cmd_npm6001, 1, 1);
