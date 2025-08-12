#ifndef __KCSDR_H__
#define __KCSDR_H__

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_NAME_LEN (50)
#define DEVICE_PORT     (6)

#ifdef __linux__
#define SYMBOL_EXPORT   
#elif defined(_WIN32) || defined(_WIN64)
#define SYMBOL_EXPORT __declspec(dllexport) 
#endif 

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
    int minimum;
    int step;
    int64_t maximum;
}par_range;
typedef struct 
{
    bool      set;
    par_range rx_freq;
    par_range tx_freq;
    par_range bw;
    par_range att;
    par_range amp;
    par_range ifgain;
    par_range samp_rate;
}dev_range;
typedef enum 
{
    INT_16,
    INT_32,
    FLOAT_32,
}data_type;

typedef enum
{
    DEV_RUNNING,
    DEV_DISCONNECTED,
    DEV_ERR,
}dev_status;
typedef struct 
{
    data_type type;
    uint16_t  iq_pair;
}data_val;

#define CAL_ATT_STEP (3)
#define CAL_IN_AMP_STEP (5)
#define CAL_EXT_AMP_STEP (2)

typedef struct 
{
    uint8_t     port;
    uint64_t    freq;
    float       lev;
    uint8_t     status;
}cal_cmd;

typedef struct 
{
    uint64_t freq;
    float base;
    float att[CAL_ATT_STEP];
    float amp[CAL_IN_AMP_STEP];
    float ext_amp[CAL_EXT_AMP_STEP];
    float rssi_limit;
    float field_limit;
    uint8_t status;
}cal_fr_ret;

typedef struct
{
    cal_cmd     cmd;
    cal_fr_ret  data;
}cal_fr;

typedef struct 
{
    bool (*find)(char *name, int **private_val);
    void (*close)(int *private_val); 
    void (*freq)(uint64_t freq, int *private_val, bool is_rx);  
    void (*port)(uint8_t port, int *private_val, bool is_rx);
    void (*bw)(uint32_t bw, int *private_val, bool is_rx);
    void (*fe_att)(uint8_t att, int *private_val, bool is_rx);
    void (*fe_amp)(uint8_t amp, int *private_val, bool is_rx);
    void (*fe_ext_amp)(uint8_t amp, int *private_val, bool is_rx);
    void (*start)(int *private_val, bool is_rx);
    void (*stop)(int *private_val, bool is_rx);
    bool (*read)(int *private_val, uint8_t *buf, uint32_t size);
    bool (*write)(int *private_val, uint8_t *buf, uint32_t size);
    dev_status (*status)(int *private_val);

    bool (*serial_num_get)(int *private_val, uint8_t *buf); 
    void (*data_get)(int *private_val, data_val *data); 
    void (*port_get)(int *private_val, dev_range *port); 
    uint32_t (*bw_get)(int *private_val, uint16_t index);
    void *(*fr_cal)(int *private_val, cal_fr * fr);
}device_op;

typedef enum
{
    KC_908_1,
    KC_908_N,
}device_type;

typedef struct
{
    char            name[DEVICE_NAME_LEN];
    char            serial_num[DEVICE_NAME_LEN];
    data_val        data;
    dev_range       port[DEVICE_PORT];
    int             *private_val;
    device_op       operation;
}sdr_obj;

typedef struct 
{
    sdr_obj *(*find)(device_type type);
    void (*close)(sdr_obj *obj);
    void (*rx_freq)(sdr_obj *obj, uint64_t freq);
    void (*rx_port)(sdr_obj *obj, uint8_t port);
    void (*rx_bw)(sdr_obj *obj, uint32_t bw);
    void (*rx_att)(sdr_obj *obj, uint8_t att);
    void (*rx_amp)(sdr_obj *obj, uint8_t amp);
    void (*rx_ext_amp)(sdr_obj *obj, uint8_t amp);
    void (*rx_samp_rate)(sdr_obj *obj, uint8_t amp);
    void (*rx_start)(sdr_obj *obj);
    void (*rx_stop)(sdr_obj *obj);  
    void *(*fr_cal)(sdr_obj *obj, cal_fr * fr);
    void (*tx_freq)(sdr_obj *obj, uint64_t freq);
    void (*tx_port)(sdr_obj *obj, uint8_t port);
    void (*tx_bw)(sdr_obj *obj, uint32_t bw);
    void (*tx_att)(sdr_obj *obj, uint8_t att);
    void (*tx_amp)(sdr_obj *obj, uint8_t amp);
    void (*tx_samp_rate)(sdr_obj *obj, uint8_t amp);
    void (*tx_start)(sdr_obj *obj);
    void (*tx_stop)(sdr_obj *obj); 
    dev_status (*status)(sdr_obj *obj);

    bool (*read)(sdr_obj *obj, uint8_t *buf, uint32_t size);
    bool (*write)(sdr_obj *obj, uint8_t *buf, uint32_t size);
    uint32_t(*bw_get)(sdr_obj *obj, uint16_t index);     
}sdr_api;

extern SYMBOL_EXPORT sdr_api * kcsdr_init(void);
#ifdef __cplusplus
}
#endif
#endif
