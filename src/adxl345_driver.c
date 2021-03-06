/*
 * adxl345_driver.c
 *
 *  Created on: Jan 10, 2016
 *      Author: petera
 */

#include "adxl345_driver.h"


static void adxl_cb(i2c_dev *idev, int res) {
  adxl345_dev *dev = (adxl345_dev *)I2C_DEV_get_user_data(idev);
  adxl_state old_state = dev->state;
  if (res != I2C_OK) {
    dev->full_conf = FALSE;
    dev->state = ADXL345_STATE_IDLE;
    if (dev->callback) dev->callback(dev, old_state, res);
    return;
  }

  switch (dev->state) {
  case ADXL345_STATE_CONFIG_POWER:
    if (dev->full_conf)
      res = adxl_config_tap(dev,
          dev->arg.cfg->tap_ena, dev->arg.cfg->tap_thresh, dev->arg.cfg->tap_dur,
          dev->arg.cfg->tap_latent, dev->arg.cfg->tap_window, dev->arg.cfg->tap_suppress);
    break;
  case ADXL345_STATE_SET_OFFSET:
    break;
  case ADXL345_STATE_CONFIG_TAP:
    if (dev->full_conf)
      res = adxl_config_activity(dev,
          dev->arg.cfg->act_ac_dc, dev->arg.cfg->act_ena, dev->arg.cfg->act_inact_ena,
          dev->arg.cfg->act_thr_act, dev->arg.cfg->act_thr_inact, dev->arg.cfg->act_time_inact);
    break;
  case ADXL345_STATE_CONFIG_ACTIVITY:
    if (dev->full_conf)
      res = adxl_config_freefall(dev,
          dev->arg.cfg->freefall_thresh, dev->arg.cfg->freefall_time);
    break;
  case ADXL345_STATE_CONFIG_FREEFALL:
    if (dev->full_conf)
      res = adxl_config_interrupts(dev,
          dev->arg.cfg->int_ena, dev->arg.cfg->int_map);
    break;
  case ADXL345_STATE_CONFIG_INTERRUPTS:
    if (dev->full_conf)
      res = adxl_config_format(dev,
          dev->arg.cfg->format_int_inv, dev->arg.cfg->format_full_res, dev->arg.cfg->format_justify,
          dev->arg.cfg->format_range);
    break;
  case ADXL345_STATE_CONFIG_FORMAT:
    if (dev->full_conf)
      res = adxl_config_fifo(dev,
          dev->arg.cfg->fifo_mode, dev->arg.cfg->fifo_trigger, dev->arg.cfg->fifo_samples);
    break;
  case ADXL345_STATE_CONFIG_FIFO:
    if (dev->full_conf) dev->full_conf = FALSE;
    break;

  case ADXL345_STATE_READ:
    if (dev->arg.data) {
      dev->arg.data->x = (dev->tmp_buf[2] << 8) | dev->tmp_buf[1];
      dev->arg.data->y = (dev->tmp_buf[4] << 8) | dev->tmp_buf[3];
      dev->arg.data->z = (dev->tmp_buf[6] << 8) | dev->tmp_buf[5];
    }
    break;

  case ADXL345_STATE_READ_INTERRUPTS:
    if (dev->arg.int_src) {
      *dev->arg.int_src = dev->tmp_buf[1];
    }
    break;

  case ADXL345_STATE_READ_FIFO:
    if (dev->arg.fifo_status) {
      dev->arg.fifo_status->raw = dev->tmp_buf[1];
    }
    break;

  case ADXL345_STATE_READ_ACT_TAP:
    if (dev->arg.act_tap_status) {
      dev->arg.act_tap_status->raw = dev->tmp_buf[1];
    }
    break;

  case ADXL345_STATE_READ_ALL_STATUS:
    if (dev->arg.status) {
      dev->arg.status->int_src = dev->tmp_buf[3];
      dev->arg.status->act_tap_status.raw = dev->tmp_buf[4];
      dev->arg.status->fifo_status.raw = dev->tmp_buf[5];
    }
    break;

  case ADXL345_STATE_ID:
    if (dev->arg.id_ok) {
      *dev->arg.id_ok =
          dev->tmp_buf[1] == ADXL345_ID;
    }
    break;

  case ADXL345_STATE_IDLE:
  default:
    res = I2C_ERR_ADXL345_STATE;
    break;
  }

  if (!dev->full_conf || res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
    dev->full_conf = FALSE;
  }

  if (dev->state == ADXL345_STATE_IDLE) {
    if (dev->callback) dev->callback(dev, old_state, res);
  }
}

void adxl_open(adxl345_dev *dev, i2c_bus *bus, u32_t clock,
    void (*adxl_callback)(adxl345_dev *dev, adxl_state state, int res)) {
  memset(dev, 0, sizeof(adxl345_dev));
  I2C_DEV_init(&dev->i2c_dev, clock, bus, ADXL345_I2C_ADDR);
  I2C_DEV_set_user_data(&dev->i2c_dev, dev);
  I2C_DEV_set_callback(&dev->i2c_dev, adxl_cb);
  I2C_DEV_open(&dev->i2c_dev);
  dev->callback = adxl_callback;
}

void adxl_close(adxl345_dev *dev) {
  I2C_DEV_close(&dev->i2c_dev);
}


int adxl_check_id(adxl345_dev *dev, bool *id_ok) {
  if (id_ok == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.id_ok = id_ok;
  dev->tmp_buf[0] = ADXL345_R_DEVID;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 1);
  dev->state = ADXL345_STATE_ID;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config(adxl345_dev *dev, const adxl_cfg *cfg) {
  if (cfg == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->full_conf = TRUE;
  dev->arg.cfg = cfg;
  return adxl_config_power(dev, cfg->pow_low_power, cfg->pow_rate, cfg->pow_link,
      cfg->pow_auto_sleep, cfg->pow_mode, cfg->pow_sleep, cfg->pow_sleep_rate);
}


int adxl_config_power(adxl345_dev *dev,
    bool low_power, adxl_rate rate,
    bool link, bool auto_sleep, adxl_mode mode,
    bool sleep, adxl_sleep_rate sleep_rate) {
  // ADXL345_R_BW_RATE 0x2c low_power, rate
  // ADXL345_R_POWER_CTL 0x2d link, auto_sleep, mode, sleep, sleep_rate
  dev->tmp_buf[0] = ADXL345_R_BW_RATE;
  dev->tmp_buf[1] = 0 |
      (low_power ? (1<<4) : 0) |
      rate;
  dev->tmp_buf[2] = 0 |
      (link ? (1<<5) : 0) |
      (auto_sleep ? (1<<4) : 0) |
      (mode<<3) |
      (sleep ? (1<<2) : 0) |
      sleep_rate;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 3);
  dev->state = ADXL345_STATE_CONFIG_POWER;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_set_offset(adxl345_dev *dev, s8_t x, s8_t y, s8_t z) {
  // ADXL345_R_OFSX 0x1e x
  // ADXL345_R_OFSY 0x1f y
  // ADXL345_R_OFSZ 0x20 z
  dev->tmp_buf[0] = ADXL345_R_OFSX;
  dev->tmp_buf[1] = x;
  dev->tmp_buf[2] = y;
  dev->tmp_buf[3] = z;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 4);
  dev->state = ADXL345_STATE_SET_OFFSET;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_tap(adxl345_dev *dev,
    adxl_axes tap_ena, u8_t thresh, u8_t dur, u8_t latent, u8_t window, bool suppress) {
  // ADXL345_R_TAP_AXES 0x2a suppress, tap_ena
  // ADXL345_R_THRESH_TAP 0x1d thresh
  // ADXL345_R_DUR 0x21 dur
  // ADXL345_R_LATENT 0x22 latent
  // ADXL345_R_WINDOW 0x23 window
  dev->tmp_buf[0] = ADXL345_R_TAP_AXES;
  dev->tmp_buf[1] = 0 |
      (suppress ? (1<<3) : 0) |
      tap_ena;
  dev->tmp_buf[2] = ADXL345_R_THRESH_TAP;
  dev->tmp_buf[3] = thresh;
  dev->tmp_buf[4] = ADXL345_R_DUR;
  dev->tmp_buf[5] = dur;
  dev->tmp_buf[6] = latent;
  dev->tmp_buf[7] = window;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 2);
  I2C_SEQ_TX_STOP_C(dev->seq[1], &dev->tmp_buf[2], 2);
  I2C_SEQ_TX_STOP_C(dev->seq[2], &dev->tmp_buf[4], 4);
  dev->state = ADXL345_STATE_CONFIG_TAP;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 3);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_activity(adxl345_dev *dev,
    adxl_acdc ac_dc, adxl_axes act_ena, adxl_axes inact_ena, u8_t thr_act, u8_t thr_inact, u8_t time_inact) {
  // ADXL345_R_THRESH_ACT 0x24 thr_act
  // ADXL345_R_THRESH_INACT 0x25 thr_inact
  // ADXL345_R_TIME_INACT 0x26 time_inact
  // ADXL345_R_ACT_INACT_CTL 0x27 ac_dc, act_ena, inact_ena
  dev->tmp_buf[0] = ADXL345_R_THRESH_ACT;
  dev->tmp_buf[1] = thr_act;
  dev->tmp_buf[2] = thr_inact;
  dev->tmp_buf[3] = time_inact;
  dev->tmp_buf[4] = 0 |
      (ac_dc<<7) |
      (act_ena<<4) |
      (ac_dc<<3) |
      (inact_ena);

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 5);
  dev->state = ADXL345_STATE_CONFIG_ACTIVITY;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_freefall(adxl345_dev *dev, u8_t thresh, u8_t time) {
  // ADXL345_R_THRESH_FF 0x28 thresh
  // ADXL345_R_TIME_FF 0x29 time
  dev->tmp_buf[0] = ADXL345_R_THRESH_FF;
  dev->tmp_buf[1] = thresh;
  dev->tmp_buf[2] = time;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 3);
  dev->state = ADXL345_STATE_CONFIG_FREEFALL;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_interrupts(adxl345_dev *dev, u8_t int_ena, u8_t int_map) {
  // ADXL345_R_INT_ENABLE 0x2e int_ena
  // ADXL345_R_INT_MAP 0x2f int_map
  dev->tmp_buf[0] = ADXL345_R_INT_ENABLE;
  dev->tmp_buf[1] = int_ena;
  dev->tmp_buf[2] = int_map;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 3);
  dev->state = ADXL345_STATE_CONFIG_INTERRUPTS;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_format(adxl345_dev *dev,
    bool int_inv, bool full_res, bool justify, adxl_range range) {
  // ADXL345_R_DATA_FORMAT 0x31 int_inv, full_res, justify, range
  dev->tmp_buf[0] = ADXL345_R_DATA_FORMAT;
  dev->tmp_buf[1] = 0 |
      (int_inv ? (1<<5) : 0) |
      (full_res ? (1<<3) : 0) |
      (justify ? (1<<2): 0) |
      range;

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 2);
  dev->state = ADXL345_STATE_CONFIG_FORMAT;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_config_fifo(adxl345_dev *dev,
    adxl_fifo_mode mode, adxl_pin_int trigger, u8_t samples) {
  // ADXL345_R_FIFO_CTL 0x38 mode, trigger, samples
  dev->tmp_buf[0] = ADXL345_R_FIFO_CTL;
  dev->tmp_buf[1] = 0 |
      (mode ? (1<<6) : 0) |
      (trigger ? (1<<5) : 0) |
      (samples & 0x1f);

  I2C_SEQ_TX_STOP_C(dev->seq[0], &dev->tmp_buf[0], 2);
  dev->state = ADXL345_STATE_CONFIG_FIFO;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 1);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_read_data(adxl345_dev *dev, adxl_reading *data) {
  if (data == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.data = data;
  dev->tmp_buf[0] = ADXL345_R_DATAX0;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 6);
  dev->state = ADXL345_STATE_READ;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_read_interrupts(adxl345_dev *dev, u8_t *int_src) {
  if (int_src == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.int_src = int_src;
  dev->tmp_buf[0] = ADXL345_R_INT_SOURCE;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 1);
  dev->state = ADXL345_STATE_READ_INTERRUPTS;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_read_fifo_status(adxl345_dev *dev, adxl_fifo_status *fifo_status) {
  if (fifo_status == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.fifo_status = fifo_status;
  dev->tmp_buf[0] = ADXL345_R_FIFO_STATUS;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 1);
  dev->state = ADXL345_STATE_READ_FIFO;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

int adxl_read_act_tap_status(adxl345_dev *dev, adxl_act_tap_status *act_tap_status) {
  if (act_tap_status == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.act_tap_status = act_tap_status;
  dev->tmp_buf[0] = ADXL345_R_ACT_TAP_STATUS;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 1);
  dev->state = ADXL345_STATE_READ_ACT_TAP;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}


int adxl_read_status(adxl345_dev *dev, adxl_status *status) {
  if (status == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.status = status;
  dev->tmp_buf[0] = ADXL345_R_INT_SOURCE;
  dev->tmp_buf[1] = ADXL345_R_ACT_TAP_STATUS;
  dev->tmp_buf[2] = ADXL345_R_FIFO_STATUS;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[3], 1);
  I2C_SEQ_TX_C(dev->seq[2], &dev->tmp_buf[1], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[3], &dev->tmp_buf[4], 1);
  I2C_SEQ_TX_C(dev->seq[4], &dev->tmp_buf[2], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[5], &dev->tmp_buf[5], 1);
  dev->state = ADXL345_STATE_READ_ALL_STATUS;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 6);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

/////////////////////////////////////////////////////////////////////////// CLI

#ifndef CONFIG_CLI_ADXL345_OFF
#include "cli.h"
#include "miniutils.h"

static adxl345_dev adxl_dev;
static bool adxl_bool;
static adxl_reading adxl_data;
static adxl_status adxl_sr;
static volatile bool adxl_busy;

static void cli_adxl_cb(adxl345_dev *dev, adxl_state state, int res) {
  if (res < 0) print("adxl_cb err %i\n", res);
  switch (state) {
  case ADXL345_STATE_ID:
    print("adxl id ok: %s\n", adxl_bool ? "TRUE":"FALSE");
    break;
  case ADXL345_STATE_READ:
    print("adxl data: %04x %04x %04x\n", adxl_data.x, adxl_data.y, adxl_data.z);
    break;
  case ADXL345_STATE_READ_ALL_STATUS:
    print("adxl state:\n"
        "  int raw       : %08b\n"
        "  int dataready : %i\n"
        "  int activity  : %i\n"
        "  int inactivity: %i\n"
        "  int sgl tap   : %i\n"
        "  int dbl tap   : %i\n"
        "  int freefall  : %i\n"
        "  int overrun   : %i\n"
        "  int watermark : %i\n"
        "  acttapsleep   : %08b\n"
        "  act x y z     : %i %i %i\n"
        "  tap x y z     : %i %i %i\n"
        "  sleep         : %i\n"
        "  fifo trigger  : %i\n"
        "  entries       : %i\n"
        ,
        adxl_sr.int_src,
        (adxl_sr.int_src & ADXL345_INT_DATA_READY) != 0,
        (adxl_sr.int_src & ADXL345_INT_ACTIVITY) != 0,
        (adxl_sr.int_src & ADXL345_INT_INACTIVITY) != 0,
        (adxl_sr.int_src & ADXL345_INT_SINGLE_TAP) != 0,
        (adxl_sr.int_src & ADXL345_INT_DOUBLE_TAP) != 0,
        (adxl_sr.int_src & ADXL345_INT_FREE_FALL) != 0,
        (adxl_sr.int_src & ADXL345_INT_OVERRUN) != 0,
        (adxl_sr.int_src & ADXL345_INT_WATERMARK) != 0,
        adxl_sr.act_tap_status,
        adxl_sr.act_tap_status.act_x,
        adxl_sr.act_tap_status.act_y,
        adxl_sr.act_tap_status.act_z,
        adxl_sr.act_tap_status.tap_x,
        adxl_sr.act_tap_status.tap_y,
        adxl_sr.act_tap_status.tap_z,
        adxl_sr.act_tap_status.asleep,
        adxl_sr.fifo_status.fifo_trig,
        adxl_sr.fifo_status.entries

        );
    break;
  case ADXL345_STATE_CONFIG_ACTIVITY:
  case ADXL345_STATE_CONFIG_FIFO:
  case ADXL345_STATE_CONFIG_FORMAT:
  case ADXL345_STATE_CONFIG_FREEFALL:
  case ADXL345_STATE_CONFIG_INTERRUPTS:
  case ADXL345_STATE_CONFIG_POWER:
  case ADXL345_STATE_CONFIG_TAP:
    print("adxl cfg ok: %02x\n", state);
    break;
  default:
    print("adxl_cb unknown state %02x\n", state);
    break;
  }
  adxl_busy = FALSE;
}

static s32_t cli_adxl_open(u32_t argc, u8_t bus, u32_t speed) {
  if (argc == 1) {
    speed = 100000;
  } else if (argc != 2) {
    return CLI_ERR_PARAM;
  }
  adxl_open(&adxl_dev, _I2C_BUS(bus), speed, cli_adxl_cb);
  int res = adxl_check_id(&adxl_dev, &adxl_bool);
  return res;
}

static s32_t cli_adxl_close(u32_t argc) {
  adxl_close(&adxl_dev);
  return CLI_OK;
}

static s32_t cli_adxl_cfg(u32_t argc) {
  int res;

  adxl_busy = TRUE;
  res = adxl_config_power(&adxl_dev, FALSE, ADXL345_RATE_12_5_LP, TRUE, FALSE, ADXL345_MODE_MEASURE, FALSE, ADXL345_SLEEP_RATE_8);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_fifo(&adxl_dev, ADXL345_FIFO_BYPASS, ADXL345_PIN_INT1, 0);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_format(&adxl_dev, FALSE, TRUE, FALSE, ADXL345_RANGE_2G);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_freefall(&adxl_dev, 0, 0);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_interrupts(&adxl_dev, ADXL345_INT_SINGLE_TAP | ADXL345_INT_INACTIVITY | ADXL345_INT_ACTIVITY, 0);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_tap(&adxl_dev, ADXL345_XYZ, 0x40, 0x30, 0x40, 0xff , FALSE);
  if (res != 0) return res;
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_activity(&adxl_dev, ADXL345_AC, ADXL345_XYZ, ADXL345_XYZ, 12, 24, 5);
  if (res != 0) return res;
  while (adxl_busy);

  return CLI_OK;
}

static s32_t cli_adxl_read(u32_t argc) {
  int res = adxl_read_data(&adxl_dev, &adxl_data);
  if (res != 0) return res;
  return CLI_OK;
}

static s32_t cli_adxl_stat(u32_t argc) {
  int res = adxl_read_status(&adxl_dev, &adxl_sr);
  if (res != 0) return res;
  return CLI_OK;
}

CLI_MENU_START(adxl345)
CLI_FUNC("cfg", cli_adxl_cfg, "Configures adxl345 device\n"
        "cfg (TODO)\n"
        "ex: cfg\n")
CLI_FUNC("close", cli_adxl_close, "Closes adxl345 device")
CLI_FUNC("open", cli_adxl_open, "Opens adxl345 device\n"
        "open <bus> (<bus_speed>)\n"
        "ex: open 0 100000\n")
CLI_FUNC("rd", cli_adxl_read, "Reads accelerometer values")
CLI_FUNC("stat", cli_adxl_stat, "Reads adxl345 status")
CLI_MENU_END

#endif
