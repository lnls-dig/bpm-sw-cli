#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <acq_client.h>
#include <halcs_client.h>

#define DFLT_BIND_FOLDER "/tmp/bpm"

#define DEFAULT_NUM_SAMPLES         4096
#define DEFAULT_CHAN_NUM            0

#define DFLT_BPM_NUMBER             0

#define DFLT_BOARD_NUMBER           0

#define FMC130M_4CH_MODULE_NAME     "FMC130M_4CH"
#define FMC250M_4CH_MODULE_NAME     "FMC250M_4CH"
#define FMC_ADC_COMMON_MODULE_NAME  "FMC_ADC_COMMON"
#define FMC_ACTIVE_CLK_MODULE_NAME  "FMC_ACTIVE_CLK"
#define DSP_MODULE_NAME             "DSP"
#define SWAP_MODULE_NAME            "SWAP"
#define ACQ_MODULE_NAME             "ACQ"
#define RFFE_MODULE_NAME            "RFFE"
#define MAX_VARIABLES_NUMBER        sizeof(uint32_t)*8

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Arbitrary hard limits */
#define MAX_NUM_SAMPLES             (1 << 28)
#define MAX_NUM_CHANS               (1 << 8)

#define PRINTV(verbose, fmt, ...)\
    do {\
        if (verbose) {\
            printf (fmt, ## __VA_ARGS__);\
        }\
    }while(0)

typedef enum {
    TEXT = 0,
    BINARY,
    END_FILE_FMT
} filefmt_e;

void print_data_curve (uint32_t chan, uint32_t *data, uint32_t size, filefmt_e filefmt)
{
    /* FIXME: Make it more generic */
    if (chan == 0 || chan == 1 /* Only ADC and ADC SWAP */ ) {
        int16_t *raw_data16 = (int16_t *) data;
        if (filefmt == TEXT) {
            for (uint32_t i = 0; i < (size/sizeof(uint16_t)) / 4; i++) {
                if (zctx_interrupted) {
                    break;
                }

                printf ("%8d\t %8d\t %8d\t %8d\n",
                        raw_data16[(i*4)],
                        raw_data16[(i*4)+1],
                        raw_data16[(i*4)+2],
                        raw_data16[(i*4)+3]);
            }
        }
        else if (filefmt == BINARY) {
            fwrite (raw_data16, 2, size/2, stdout);
        }
    }
    else {
        int32_t *raw_data32 = (int32_t *) data;
        if (filefmt == TEXT) {
            for (uint32_t i = 0; i < (size/sizeof(uint32_t)) / 4; i++) {
                if (zctx_interrupted) {
                    break;
                }

                printf ("%8d\t %8d\t %8d\t %8d\n",
                        raw_data32[(i*4)],
                        raw_data32[(i*4)+1],
                        raw_data32[(i*4)+2],
                        raw_data32[(i*4)+3]);
            }
        }
        else if (filefmt == BINARY) {
            fwrite (raw_data32, 4, size/4, stdout);
        }
    }
}

typedef struct _call_var_t {
    char *name;
    char *service;
    int rw;
    int poll;
    uint32_t write_val[MAX_VARIABLES_NUMBER];
    uint32_t read_val[MAX_VARIABLES_NUMBER];
} call_var_t;

typedef call_var_t call_func_t;

static void _zlist_free_item (void *data)
{
    free(((call_func_t *) data)->name);
    free((call_func_t *) data);
    data = NULL;
}

void append_item (zlist_t* list, call_func_t func)
{
    call_func_t *wrap_func = zmalloc(sizeof(call_func_t));
    *wrap_func = func;
    wrap_func->name = strdup(func.name);
    zlist_append (list, wrap_func);
    zlist_freefn (list, wrap_func, _zlist_free_item, false);
}

int print_var (call_var_t *var)
{
    const disp_op_t* func_structure = halcs_func_translate (var->name);

    switch (DISP_GET_ATYPE(func_structure->retval))
    {
        case DISP_ATYPE_UINT16:;
            uint16_t* read_val_ptr16 = (uint16_t *)var->read_val; /* Avoid strict-aliasing breaking */
            printf ("%" PRIu16 "\n", *(read_val_ptr16));
            break;

        case DISP_ATYPE_UINT32:;
            uint32_t* read_val_ptr32 = (uint32_t *)var->read_val; /* Avoid strict-aliasing breaking */
            printf ("%" PRIu32 "\n", *(read_val_ptr32));
            break;

        case DISP_ATYPE_UINT64:;
            uint64_t* read_val_ptr64 = (uint64_t *)var->read_val; /* Avoid strict-aliasing breaking */
            printf ("%" PRIu64 "\n", *(read_val_ptr64));
            break;

        case DISP_ATYPE_DOUBLE:;
            double* read_val_ptr_dbl = (double *)var->read_val;
            printf ("%f\n", *(read_val_ptr_dbl));
            break;

        default:
            printf ("%" PRIu16 "\n", ((uint16_t )*(var->read_val)));
    }
    return 0;
}

int print_var_v(int verbose, call_var_t *func)
{
    PRINTV(verbose, "%s: ", func->name);
    if (verbose) {
        print_var(func);
    }
    return 0;
}

int print_func_v(int verbose, call_func_t *func)
{
    return print_var_v(verbose, (call_var_t *)func);
}

enum
{
    CHANNEL = 0,
    VALUE,
    SUBOPT_END
};

char *mount_opts[] =
{
    [CHANNEL] = "chan",
    [VALUE] = "value",
    [SUBOPT_END] = NULL
};

halcs_client_err_e parse_subopt (char *subopts, char *mount_opts[], char* name, char *corr_name, uint32_t *input)
{
    halcs_client_err_e err = HALCS_CLIENT_SUCCESS;
    strncpy(corr_name, "", strlen(corr_name));
    char* value;
    char* temp_value = "";
    size_t len = strlen(name);
    char* temp_name = zmalloc(len);
    memcpy(temp_name, name, len-1);
    memcpy(corr_name, temp_name, len);

    while (*subopts != '\0')
    {
        switch (getsubopt (&subopts, mount_opts, &value))
        {
            case CHANNEL:
                    memcpy(corr_name+len-1, value, strlen(value));
                    memcpy(corr_name+len, "\0", 1);
                    break;
            case VALUE:
                    temp_value = value;
                    break;
            default:
                    /* Unknown suboption. */
                    fprintf (stderr, "Unknown suboption '%s'\n", value);
                    err = HALCS_CLIENT_ERR_INV_FUNCTION;
                    goto inv_function;
        }
    }
    const disp_op_t* temp_func = halcs_func_translate(corr_name);
    if (temp_func == NULL) {
        err = HALCS_CLIENT_ERR_INV_FUNCTION;
        goto inv_function;
    }

    if (DISP_GET_ATYPE(temp_func->args[1]) == DISP_ATYPE_DOUBLE) {
        *(double *)(input+4) = strtod(temp_value, NULL);
    } else {
        *(input+4) = strtoul(temp_value, NULL, 10);
    }

inv_function:
    free (temp_name);
    value = NULL;
    temp_name = NULL;
    return err;
}

void print_usage (const char *program_name, FILE* stream, int exit_code)
{
    /* FIXME: Add the RFFE module functions' help information */
    fprintf (stream, "HALCS Client program\n");
    fprintf (stream, "Usage:  %s options \n", program_name);
    fprintf (stream,
            "  -h  --help                       Display this usage information.\n"
            "  -v  --verbose                    Print verbose messages.\n"
            "  -e  --endpoint <endpoint>        Define broker endpoint\n"
            "  -d  --board <number>             Define the target AFC board\n"
            "  -m  --bpm <0 | 1>                Define the target FMC board\n"
            "  -l  --leds <value>               Set board leds\n"
            "                                    [value must be between 0 and 7 (3 bits),\n"
            "                                     each bit sets one rgb led color\n"
            "  -p  --getpll                     Get PLL value\n"
            "  -P  --setpll <value>             Set PLL value\n"
            "  -L  --ad9510default              Set AD9510 to default values\n"
            "  -c  --getadcdata                 Get ADC data\n"
            "  --getdlyval                      Get delay value\n"
            "  --setdlyval <value>              Set delay value\n"
            "  --getdlyline                     Get delay line\n"
            "  --setdlyline <value>             Set delay line\n"
            "  --getdlyupdt                     Get delay update\n"
            "  --setdlyupdt <value>             Set delay update\n"
            "  -V  --setadcdly <value>          Set ADC delay\n"
            "  -n  --getadctest                 Get ADC test\n"
            "  -N  --setadctest <value>         Set ADC test\n"
            "  -o  --getsi571oe                 Get SI571 OE\n"
            "  -O  --setsi571oe <value>         Set SI571 OE\n"
            "  -i  --setsi571freq <value [Hz]>  Set SI571 frequency\n"
            "  -D  --si571default               Set SI571 to default values\n"
            "  -a  --setad9510plladiv <value>   Set AD9510 PLL A Divider\n"
            "  -b  --setad9510pllbdiv <value>   Set AD9510 PLL B Divider\n"
            "  -r  --setad9510pllpresc <value>  Set AD9510 PLL Prescaler\n"
            "  -R  --setad9510rdiv <value>      Set AD9510 R Divider\n"
            "  -B  --setad9510pdown <value>     Set AD9510 PDown\n"
            "  -M  --setad9510mux <value>       Set AD9510 Multiplexer\n"
            "  -u  --setad9510cpcurr <value>    Set AD9510 CP Current\n"
            "  -U  --setad9510outputs <value>   Set AD9510 Outputs\n"
            "  -k  --setad9510pllclksel <value> Set AD9510 PLL Clock Selection\n"
            "  --getkx                          Get KX value\n"
            "  --setkx <value>                  Set KX value\n"
            "  --getky                          Get KY value\n"
            "  --setky <value>                  Set KY value\n"
            "  --getksum                        Get KSUM value\n"
            "  --setksum <value>                Set KSUM value\n"
            "  --gettbtth                       Get TBT threshold value\n"
            "  --settbtth <value>               Set TBT threshold value\n"
            "  --getfofbth                      Get FOFB threshold value\n"
            "  --setfofbth <value>              Set FOFB threshold value\n"
            "  --getmonitth                     Get Monitoring threshold value\n"
            "  --setmonitth <value>             Set Monitoring threshold value\n"
            "  -j  --getmonitamp                Get Monitoring amplitude\n"
            "  -x  --getmonitposx               Get Monitoring X position\n"
            "  -y  --getmonitposy               Get Monitoring Y position\n"
            "  -q  --getmonitposq               Get Monitoring position Q\n"
            "  -s  --getmonitpossum             Get Monitoring position Sum\n"
            "  -w  --getsw                      Get SW status\n"
            "  -W  --setsw <value>              Set SW status\n"
            "  -z  --getdivclk                  Get divider clock\n"
            "  -Z  --setdivclk <value>          Set divider clock\n"
            "  -f  --getswdly                   Get SW delay\n"
            "  -F  --setswdly <value>           Set SW delay\n"
            "  --rffesetatt <0-31.5 [dB] >    Set RFFE attenuation\n"
            "  --rffegetatt                     Get RFFE attenuation\n"
            "  --rffesettemp  <chan=(1|2), value=[degrees]>\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "                                   Set RFFE board temperature\n"
            "  --rffegettemp  <chan=(1|2)>      Get RFFE board temperature\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "  --rffesetpoint <chan=(1|2), value=[degrees]>\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "                                   Set RFFE Temperature set-point\n"
            "  --rffegetpoint <chan=(1|2)>      Get RFFE Temperature set-point\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "  --rffesettempctr < 0|1 >         Set RFFE Temperature control status\n"
            "  --rffegettempctr                 Get RFFE Temperature control status\n"
            "  --rffesetout <chan=(1|2), value=[degrees]>\n"
            "                                   Set RFFE Voltage signal in the heater\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "  --rffegetout <chan=(1|2)>        Get RFFE Voltage signal in the heater\n"
            "                                   (chan=1 -> A/C ; chan=2 -> B/D)\n"
            "  --rffereset                      Resets the RFFE controller\n"
            "  --setacqtrig <type>              Set acquisition trigger type\n"
            "                                     [<type> must be one of the following:\n"
            "                                     0 -> skip trigger; 1 -> wait for external trigger\n"
            "                                     2 -> wait for data-driven trigger; 3 -> wait for software trigger]\n"
            "  --getacqtrig                     Get acquisition trigger type\n"
            "  --setdatatrigchan <data-driven trigger channel>\n"
            "                                   Set data-driven trigger channel to monitor\n"
            "                                     [<data-driven trigger channel> Channel number to monitor for a over/under threshold event.\n"
            "                                     Must be between the following:\n"
            "                                     0 -> ADC; 1 -> ADC_SWAP; 2 -> Mixer IQ120; 3 -> Mixer IQ340;\n"
            "                                     4 -> TBT Decim IQ120; 5 -> TBT Decim IQ340; 6 -> TBT Amp;\n"
            "                                     7 -> TBT Phase; 8 -> TBT Pos; 9 -> FOFB Decim IQ120;\n"
            "                                     10 -> FOFB Decim IQ340; 11 -> FOFB Amp; 12 -> FOFB Pha;\n"
            "                                     13 -> FOFB Pos; 14 -> Monit Amp; 15 -> Monit Pha; 16 -> Monit Pos]\n"
            "  --getdatatrigchan                Get data-driven trigger channel to monitor\n"
            "  --setdatatrigpol <polarity>      Set acquisition data-driven trigger polarity\n"
            "                                     [<polarity> must be one of the following:\n"
            "                                     0 -> positive edge (0 -> 1), 1 -> negative edge (1 -> 0)]\n"
            "  --getdatatrigpol                 Get acquisition data-driven trigger polarity\n"
            "  --setdatatrigsel <data lane>\n"
            "                                   Set data-driven trigger channel data lane\n"
            "                                     [<data lane> must be one of the following:\n"
            "                                     0 -> data lane 0, 1 -> data lane 1, 2 -> data lane 2,\n"
            "                                     3 -> data lane 3]\n"
            "  --getdatatrigsel                 Get data-driven trigger channel data lane\n"
            "  --setdatatrigfilt <trigger filter>\n"
            "                                   Set data-driven trigger hysteresis filter\n"
            "                                     [<trigger filter> must be between the following:\n"
            "                                     0 -> no hysteresis and <integer number (up to 2^16-1)> -> hysteresis of\n"
            "                                     length <integer number>]\n"
            "  --getdatatrigfilt                Get data-driven trigger hysteresis filter\n"
            "  --setdatatrigthres <data threshold>\n"
            "                                   Set data-driven trigger threshold\n"
            "                                     [<data threshold> Threshold on which a data-driven trigger is generated.\n"
            "                                     Must be between the following:\n"
            "                                     <integer number (from -2^31 up to 2^31-1)]\n"
            "  --getdatatrigthres               Get data-driven trigger threshold\n"
            "  --settrigdly <trigger delay>     Set trigger delay (applies to all kinds of trigger)\n"
            "                                     [<trigger delay> Number of ADC clock cycles to delay a trigger.\n"
            "                                     Must be between the following:\n"
            "                                     <integer number (from 0 up to 2^32-1)]\n"
            "  --gettrigdly                     Get trigger delay (applies to all kinds of trigger)\n"
            "  --genswtrig                      Generate software trigger\n"
            "  --setsamplespre    <number of pre-trigger samples>\n"
            "                                     [<number of pre-trigger samples> must be between 4 and\n"
            "                                     ??? (TBD)]\n"
            "  --setsamplespost   <number of post-trigger samples>\n"
            "                                     [<number of post-trigger samples> must be between 4 and\n"
            "                                     ??? (TBD)]\n"
            "  --setnumshots      <number of shots>\n"
            "                                     [<number of shots> must be between greater than 1]\n"
            "  -H  --setchan      <channel>     Sets FPGA Acquisition channel\n"
            "                                     [<channel> must be one of the following:\n"
            "                                     0 -> ADC; 1 -> ADC_SWAP; 2 -> Mixer IQ120; 3 -> Mixer IQ340;\n"
            "                                     4 -> TBT Decim IQ120; 5 -> TBT Decim IQ340; 6 -> TBT Amp;\n"
            "                                     7 -> TBT Phase; 8 -> TBT Pos; 9 -> FOFB Decim IQ120;\n"
            "                                     10 -> FOFB Decim IQ340; 11 -> FOFB Amp; 12 -> FOFB Pha;\n"
            "                                     13 -> FOFB Pos; 14 -> Monit Amp; 15 -> Monit Pha; 16 -> Monit Pos]\n"
            "  -I  --acqstart                   Starts FPGA acquisition with the previous parameters\n"
            "  --acqstop                        Stops ongoing FPGA acquisition\n"
            "                                     <integer number (from 0 up to 2^32-1)]\n"
            "  -K  --acqcheck                   Check if the previous acquisition is over\n"
            "  --acqcheckpoll                   Keep checking if the acquisition is over for an amount of time\n"
            "                                    (Requires --timeout <timeout>) \n"
            "  -A  --getblock <block number>    Get specified data block from server \n"
            "  --getcurve                       Get a whole data curve \n"
            "  --fullacq                        Perform a full acquisition\n"
            "  --filefmt <Acquisition file format>\n"
            "                                   Sets the acquisition file format\n"
            "                                     [<Acquisition file format>\n"
            "                                     Must be between one of the following:\n"
            "                                     <0 = text mode | 1 = binary mode>]\n"
            "  --timeout    <timeout [ms]>      Sets the timeout for the polling function\n"
            );
    exit (exit_code);
}

/* Long-only options */
enum {
    pllstatus = 1000,
    getclksel,
    setclksel,
    getadcrand,
    setadcrand,
    getadcdith,
    setadcdith,
    getadcshdn,
    setadcshdn,
    getadcpga,
    setadcpga,
    getdlyval,
    setdlyval,
    getdlyline,
    setdlyline,
    getdlyupdt,
    setdlyupdt,
    gettrigdir,
    settrigdir,
    gettrigterm,
    settrigterm,
    gettrigval,
    settrigval,
    getad9510plladiv,
    getad9510pllbdiv,
    getad9510pllpresc,
    getad9510pllrdiv,
    getad9510pllpdown,
    getad9510mux,
    getad9510cpcurr,
    getad9510outputs,
    getad9510pllclksel,
    setkx,
    getkx,
    setky,
    getky,
    setksum,
    getksum,
    settbtth,
    gettbtth,
    setfofbth,
    getfofbth,
    setmonitth,
    getmonitth,
    rffesetatt,
    rffegetatt,
    rffesettemp,
    rffegettemp,
    rffesetpnt,
    rffegetpnt,
    rffesettempctr,
    rffegettempctr,
    rffesetout,
    rffegetout,
    rffereset,
    rfferpg,
    setacqtrig,
    getacqtrig,
    setdatatrigchan,
    getdatatrigchan,
    setdatatrigpol,
    getdatatrigpol,
    setdatatrigsel,
    getdatatrigsel,
    setdatatrigfilt,
    getdatatrigfilt,
    setdatatrigthres,
    getdatatrigthres,
    settrigdly,
    gettrigdly,
    genswtrig,
    acqstop,
    acqcheckpoll,
    setsamplespre,
    setsamplespost,
    setnumshots,
    getcurve,
    fullacq,
    timeout,
    filefmt
};

/* TODO: Check which 'set' functions are boolean and set them without the need of an entry value */
static struct option long_options[] =
{
    {"help",                no_argument,         NULL, 'h'},
    {"verbose",             no_argument,         NULL, 'v'},
    {"endpoint",            required_argument,   NULL, 'e'},
    {"board",               required_argument,   NULL, 'd'},
    {"bpm",                 required_argument,   NULL, 'm'},
    {"leds",                required_argument,   NULL, 'l'},
    {"pllstatus",           no_argument,         NULL, pllstatus},
    {"getpll",              no_argument,         NULL, 'p'},
    {"setpll",              required_argument,   NULL, 'P'},
    {"ad9510default",       no_argument,         NULL, 'L'},
    {"getclksel",           no_argument,         NULL, getclksel},
    {"setclksel",           required_argument,   NULL, setclksel},
    {"getadcrand",          no_argument,         NULL, getadcrand},
    {"setadcrand",          required_argument,   NULL, setadcrand},
    {"getadcdith",          no_argument,         NULL, getadcdith},
    {"setadcdith",          required_argument,   NULL, setadcdith},
    {"getadcshdn",          no_argument,         NULL, getadcshdn},
    {"setadcshdn",          required_argument,   NULL, setadcshdn},
    {"getadcpga",           no_argument,         NULL, getadcpga},
    {"setadcpga",           required_argument,   NULL, setadcpga},
    {"getadcdata",          required_argument,   NULL, 'c'},
    {"getdlyval",           required_argument,   NULL, getdlyval},
    {"setdlyval",           required_argument,   NULL, setdlyval},
    {"getdlyline",          required_argument,   NULL, getdlyline},
    {"setdlyline",          required_argument,   NULL, setdlyline},
    {"getdlyupdt",          required_argument,   NULL, getdlyupdt},
    {"setdlyupdt",          required_argument,   NULL, setdlyupdt},
    {"setadcdly",           required_argument,   NULL, 'V'},
    {"gettestdata",         no_argument,         NULL, 'n'},
    {"settestdata",         required_argument,   NULL, 'N'},
    {"getsi571oe",          no_argument,         NULL, 'o'},
    {"setsi571oe",          required_argument,   NULL, 'O'},
    {"setsi571freq",        required_argument,   NULL, 'i'},
    {"si571default",        required_argument,   NULL, 'D'},
    {"gettrigdir",          no_argument,         NULL, gettrigdir},
    {"settrigdir",          required_argument,   NULL, settrigdir},
    {"gettrigterm",         no_argument,         NULL, gettrigterm},
    {"settrigterm",         required_argument,   NULL, settrigterm},
    {"gettrigval",          no_argument,         NULL, gettrigval},
    {"settrigval",          required_argument,   NULL, settrigval},
    {"getad9510plladiv",    no_argument,         NULL, getad9510plladiv},
    {"setad9510plladiv",    required_argument,   NULL, 'a'},
    {"getad9510pllbdiv",    no_argument,         NULL, getad9510pllbdiv},
    {"setad9510pllbdiv",    required_argument,   NULL, 'b'},
    {"getad9510pllpresc",   no_argument,         NULL, getad9510pllpresc},
    {"setad9510pllpresc",   required_argument,   NULL, 'r'},
    {"getad9510rdiv",       no_argument,         NULL, getad9510pllrdiv},
    {"setad9510rdiv",       required_argument,   NULL, 'R'},
    {"getad9510pllpdown",   no_argument,         NULL, getad9510pllpdown},
    {"setad9510pllpdown",   required_argument,   NULL, 'B'},
    {"getad9510mux",        no_argument,         NULL, getad9510mux},
    {"setad9510mux",        required_argument,   NULL, 'M'},
    {"getad9510cpcurr",     no_argument,         NULL, getad9510cpcurr},
    {"setad9510cpcurr",     required_argument,   NULL, 'u'},
    {"getad9510outputs",    no_argument,         NULL, getad9510outputs},
    {"setad9510outputs",    required_argument,   NULL, 'U'},
    {"getad9510pllclksel",  no_argument,         NULL, getad9510pllclksel},
    {"setad9510pllclksel",  required_argument,   NULL, 'k'},
    {"setkx",               required_argument,   NULL, setkx},
    {"getkx",               no_argument,         NULL, getkx},
    {"setky",               required_argument,   NULL, setky},
    {"getky",               no_argument,         NULL, getky},
    {"setksum",             required_argument,   NULL, setksum},
    {"getksum",             no_argument,         NULL, getksum},
    {"settbtth",            required_argument,   NULL, settbtth},
    {"gettbtth",            no_argument,         NULL, gettbtth},
    {"setfofbth",           required_argument,   NULL, setfofbth},
    {"getfofbth",           no_argument,         NULL, getfofbth},
    {"setmonitth",          required_argument,   NULL, setmonitth},
    {"getmonitth",          no_argument,         NULL, getmonitth},
    {"getmonitamp",         required_argument,   NULL, 'j'},
    {"getmonitposx",        required_argument,   NULL, 'x'},
    {"getmonitposy",        required_argument,   NULL, 'y'},
    {"getmonitposq",        required_argument,   NULL, 'q'},
    {"getmonitpossum",      required_argument,   NULL, 's'},
    {"setsw",               required_argument,   NULL, 'W'},
    {"getsw",               no_argument,         NULL, 'w'},
    {"setdivclk",           required_argument,   NULL, 'Z'},
    {"getdivclk",           no_argument,         NULL, 'z'},
    {"setswdly",            required_argument,   NULL, 'F'},
    {"getswdly",            no_argument,         NULL, 'f'},
    {"rffesetatt",          required_argument,   NULL, rffesetatt},
    {"rffegetatt",          required_argument,   NULL, rffegetatt},
    {"rffesettemp",         required_argument,   NULL, rffesettemp},
    {"rffegettemp",         required_argument,   NULL, rffegettemp},
    {"rffesetpnt",          required_argument,   NULL, rffesetpnt},
    {"rffegetpnt",          required_argument,   NULL, rffegetpnt},
    {"rffesettempctr",      required_argument,   NULL, rffesettempctr},
    {"rffegettempctr",      no_argument,         NULL, rffegettempctr},
    {"rffesetout",          required_argument,   NULL, rffesetout},
    {"rffegetout",          required_argument,   NULL, rffegetout},
    {"rffereset",           required_argument,   NULL, rffereset},
    {"rfferpg",             required_argument,   NULL, rfferpg},
    {"setacqtrig",          required_argument,   NULL, setacqtrig},
    {"getacqtrig",          required_argument,   NULL, getacqtrig},
    {"setdatatrigchan",     required_argument,   NULL, setdatatrigchan},
    {"getdatatrigchan",     required_argument,   NULL, getdatatrigchan},
    {"setdatatrigpol",      required_argument,   NULL, setdatatrigpol},
    {"getdatatrigpol",      required_argument,   NULL, getdatatrigpol},
    {"setdatatrigsel",      required_argument,   NULL, setdatatrigsel},
    {"getdatatrigsel",      required_argument,   NULL, getdatatrigsel},
    {"setdatatrigfilt",     required_argument,   NULL, setdatatrigfilt},
    {"getdatatrigfilt",     required_argument,   NULL, getdatatrigfilt},
    {"setdatatrigthres",    required_argument,   NULL, setdatatrigthres},
    {"getdatatrigthres",    required_argument,   NULL, getdatatrigthres},
    {"settrigdly",          required_argument,   NULL, settrigdly},
    {"gettrigdly",          required_argument,   NULL, gettrigdly},
    {"genswtrig",           no_argument,         NULL, genswtrig},
    {"acqstop",             no_argument,         NULL, acqstop},
    {"setsamplespre",       required_argument,   NULL, setsamplespre},
    {"setsamplespost",      required_argument,   NULL, setsamplespost},
    {"setnumshots",         required_argument,   NULL, setnumshots},
    {"setchan",             required_argument,   NULL, 'H'},
    {"acqstart",            no_argument,         NULL, 'I'},
    {"acqcheck",            no_argument,         NULL, 'K'},
    {"acqcheckpoll",        no_argument,         NULL, acqcheckpoll},
    {"getblock",            required_argument,   NULL, 'A'},
    {"getcurve",            no_argument,         NULL, getcurve},
    {"fullacq",             no_argument,         NULL, fullacq},
    {"timeout",             required_argument,   NULL, timeout},
    {"filefmt",             required_argument,   NULL, filefmt},
    {NULL, 0, NULL, 0}
};

int main (int argc, char *argv [])
{
    int ch;
    int verbose = 0;
    char *broker_endp = NULL;
    char *default_broker_endp = strdup ("ipc://"DFLT_BIND_FOLDER);
    const char* program_name;
    program_name = argv[0];

    uint32_t bpm_number;
    uint32_t board_number;
    char *board_number_str = NULL;
    char *bpm_number_str = NULL;
    char *filefmt_str = NULL;
    int filefmt_val = 0;

    /* Acquitision parameters check variables */
    int acq_chan_set = 0;
    uint32_t acq_samples_pre_val = 10;
    uint32_t acq_samples_post_val = 0;
    uint32_t acq_num_shots_val = 1;
    uint32_t acq_total_samples_val = 0;
    uint32_t acq_chan_val = 0;
    int acq_full_call = 0;
    int acq_start_call = 0;
    int acq_check_call = 0;
    int acq_get_block = 0;
    int acq_get_curve_call = 0;
    uint32_t acq_block_id = 0;
    int check_poll = 0;
    int poll_timeout = -1;


    const char* shortopt = "hve:d:m:l:pP:Lc:u:U:V:nN:oO:i:D:a:b:r:R:B:M:u:U:k:j:xyqswW:tT:zZ:fF:H:IKA:";

    zlist_t *call_list = zlist_new();
    if (call_list == NULL) {
        fprintf(stderr, "[client]: Error in memory allocation for zlist\n");
    }

    char corr_name[50];
    call_func_t item = {0,0,0,0,{0},{0}};

    while ((ch = getopt_long_only(argc, argv, shortopt , long_options, NULL)) != -1)
    {
        double db_val = 0;
        halcs_client_err_e err = HALCS_CLIENT_SUCCESS;

        /* Get the user selected options */
        switch (ch)
        {
                /* Display Help */
            case 'h':
                print_usage(program_name, stderr, 0);

                /* Define Verbosity level */
            case 'v':
                verbose = 1;
                break;

                /* Define Broker Endpoint */
            case 'e':
                broker_endp = strdup(optarg);
                break;

                /* Define Board Number */
            case 'd':
                board_number_str = strdup(optarg);
                break;

                /* Define BPM number */
            case 'm':
                bpm_number_str = strdup(optarg);
                break;

                /* Blink FMC Leds */
            case 'l':
                item.name = FMC_ADC_COMMON_NAME_LEDS;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                *item.write_val = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get PLL Function */
            case 'p':
                item.name = FMC_ACTIVE_CLK_NAME_PLL_FUNCTION;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set PLL Function */
            case 'P':
                item.name = FMC_ACTIVE_CLK_NAME_PLL_FUNCTION;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get PLL Lock Status */
            case pllstatus:
                item.name = FMC_ACTIVE_CLK_NAME_PLL_STATUS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* AD9510 Defaults */
            case 'L':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_CFG_DEFAULTS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                append_item (call_list, item);
                break;

                /* Get Clock Selection */
            case getclksel:
                item.name = FMC_ACTIVE_CLK_NAME_CLK_SEL;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Clock Selection */
            case setclksel:
                item.name = FMC_ACTIVE_CLK_NAME_CLK_SEL;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get ADC Rand */
            case getadcrand:
                item.name = FMC130M_4CH_NAME_ADC_RAND;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set ADC Rand */
            case setadcrand:
                item.name = FMC130M_4CH_NAME_ADC_RAND;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get ADC DITH */
            case getadcdith:
                item.name = FMC130M_4CH_NAME_ADC_DITH;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set ADC DITH */
            case setadcdith:
                item.name = FMC130M_4CH_NAME_ADC_DITH;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get ADC SHDN */
            case getadcshdn:
                item.name = FMC130M_4CH_NAME_ADC_SHDN;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set ADC SHDN */
            case setadcshdn:
                item.name = FMC130M_4CH_NAME_ADC_SHDN;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get ADC PGA */
            case getadcpga:
                item.name = FMC130M_4CH_NAME_ADC_PGA;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set ADC PGA */
            case setadcpga:
                item.name = FMC130M_4CH_NAME_ADC_PGA;
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get ADC Data */
            case 'c':
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DATA0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set ADC Data */
            case 'C':
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DATA0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get ADC Dly Value */
            case getdlyval:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_VAL0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set ADC Dly Value */
            case setdlyval:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_VAL0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get ADC Dly Line */
            case getdlyline:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_LINE0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set ADC Dly Line */
            case setdlyline:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_LINE0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get ADC Dly Update */
            case getdlyupdt:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_UPDT0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set ADC Dly Update */
            case setdlyupdt:
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY_UPDT0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set ADC Dly */
            case 'V':
                if ((err = parse_subopt (optarg, mount_opts, FMC130M_4CH_NAME_ADC_DLY0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = FMC130M_4CH_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set Test data on ADC */
            case 'N':
                item.name = FMC_ADC_COMMON_NAME_TEST_DATA_EN;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Test data on ADC */
            case 'n':
                item.name = FMC_ADC_COMMON_NAME_TEST_DATA_EN;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set SI571 OE */
            case 'O':
                item.name = FMC_ACTIVE_CLK_NAME_SI571_OE;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get SI571 OE */
            case 'o':
                item.name = FMC_ACTIVE_CLK_NAME_SI571_OE;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set SI571 Frequency */
            case 'i':
                item.name = FMC_ACTIVE_CLK_NAME_SI571_FREQ;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                db_val = strtod(optarg, NULL);
                memcpy((item.write_val+4), &db_val, sizeof(double));
                append_item (call_list, item);
                break;

                /* Get SI571 Defaults */
            case 'D':
                item.name = FMC_ACTIVE_CLK_NAME_SI571_GET_DEFAULTS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                db_val = strtod(optarg, NULL);
                memcpy(item.write_val+4, &db_val, sizeof(double));
                append_item (call_list, item);
                break;

                /* Set Trigger Dir */
            case settrigdir:
                item.name = FMC_ADC_COMMON_NAME_TRIG_DIR;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Trigger Dir */
            case gettrigdir:
                item.name = FMC_ADC_COMMON_NAME_TRIG_DIR;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Trigger Term */
            case settrigterm:
                item.name = FMC_ADC_COMMON_NAME_TRIG_TERM;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Trigger Term */
            case gettrigterm:
                item.name = FMC_ADC_COMMON_NAME_TRIG_TERM;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Trigger Value */
            case settrigval:
                item.name = FMC_ADC_COMMON_NAME_TRIG_VAL;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Trigger Value */
            case gettrigval:
                item.name = FMC_ADC_COMMON_NAME_TRIG_VAL;
                item.service = FMC_ADC_COMMON_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 PLL A Divider */
            case 'a':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_A_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 PLL A Divider */
            case getad9510plladiv:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_A_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 PLL B Divider */
            case 'b':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_B_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 PLL B Divider */
            case getad9510pllbdiv:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_B_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 PLL Prescaler */
            case 'r':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_PRESCALER;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 PLL Prescaler */
            case getad9510pllpresc:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_PRESCALER;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 R Divider */
            case 'R':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_R_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 R Divider */
            case getad9510pllrdiv:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_R_DIV;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 PLL PDown */
            case 'B':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_PDOWN;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 PLL PDown */
            case getad9510pllpdown:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_PDOWN;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 MUX Status */
            case 'M':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_MUX_STATUS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 MUX Status */
            case getad9510mux:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_MUX_STATUS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 CP Current */
            case 'u':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_CP_CURRENT;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 CP Current */
            case getad9510cpcurr:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_CP_CURRENT;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 Outputs */
            case 'U':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_OUTPUTS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 Outputs */
            case getad9510outputs:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_OUTPUTS;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set AD9510 PLL Clock Select */
            case 'k':
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_CLK_SEL;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                 /* Get AD9510 PLL Clock Select */
            case getad9510pllclksel:
                item.name = FMC_ACTIVE_CLK_NAME_AD9510_PLL_CLK_SEL;
                item.service = FMC_ACTIVE_CLK_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /****** DSP Functions ******/

                /* Set Kx */
            case setkx:
                item.name = DSP_NAME_SET_GET_KX;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Kx */
            case getkx:
                item.name = DSP_NAME_SET_GET_KX;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Ky */
            case setky:
                item.name = DSP_NAME_SET_GET_KY;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Ky */
            case getky:
                item.name = DSP_NAME_SET_GET_KY;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Ksum */
            case setksum:
                item.name = DSP_NAME_SET_GET_KSUM;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Ksum */
            case getksum:
                item.name = DSP_NAME_SET_GET_KSUM;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set TBT Thres */
            case settbtth:
                item.name = DSP_NAME_SET_GET_DS_TBT_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get TBT Thres */
            case gettbtth:
                item.name = DSP_NAME_SET_GET_DS_TBT_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set FOFB Threshold */
            case setfofbth:
                item.name = DSP_NAME_SET_GET_DS_FOFB_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get FOFB Threshold */
            case getfofbth:
                item.name = DSP_NAME_SET_GET_DS_FOFB_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit Threshold */
            case setmonitth:
                item.name = DSP_NAME_SET_GET_DS_MONIT_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Monit Threshold */
            case getmonitth:
                item.name = DSP_NAME_SET_GET_DS_MONIT_THRES;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit Position X */
            case 'X':
                item.name = DSP_NAME_SET_GET_MONIT_POS_X;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Monit Position X */
            case 'x':
                item.name = DSP_NAME_SET_GET_MONIT_POS_X;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit Position Y */
            case 'Y':
                item.name = DSP_NAME_SET_GET_MONIT_POS_Y;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Monit Position Y */
            case 'y':
                item.name = DSP_NAME_SET_GET_MONIT_POS_Y;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit Position Q */
            case 'Q':
                item.name = DSP_NAME_SET_GET_MONIT_POS_Q;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Monit Position Q */
            case 'q':
                item.name = DSP_NAME_SET_GET_MONIT_POS_Q;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit Position SUM */
            case 'S':
                item.name = DSP_NAME_SET_GET_MONIT_POS_SUM;
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Monit Position SUM */
            case 's':
                item.name = DSP_NAME_SET_GET_MONIT_POS_SUM;
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Monit AMP */
            case 'J':
                if ((err = parse_subopt (optarg, mount_opts, DSP_NAME_SET_GET_MONIT_AMP_CH0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = DSP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get Monit AMP */
            case 'j':
                if ((err = parse_subopt (optarg, mount_opts, DSP_NAME_SET_GET_MONIT_AMP_CH0, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = DSP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /******** SWAP Module Functions ********/

                /* Set SW */
            case 'W':
                item.name = SWAP_NAME_SET_GET_SW;
                item.service = SWAP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get SW */
            case 'w':
                item.name = SWAP_NAME_SET_GET_SW;
                item.service = SWAP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set SW Delay */
            case 'F':
                item.name = SWAP_NAME_SET_GET_SW_DLY;
                item.service = SWAP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get SW Delay */
            case 'f':
                item.name = SWAP_NAME_SET_GET_SW_DLY;
                item.service = SWAP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set Div Clock */
            case 'Z':
                item.name = SWAP_NAME_SET_GET_DIV_CLK;
                item.service = SWAP_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get Div Clock */
            case 'z':
                item.name = SWAP_NAME_SET_GET_DIV_CLK;
                item.service = SWAP_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /******** RFFE Module Functions *******/

                /* Set RFFE Attenuators */
            case rffesetatt:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_ATT, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get RFFE Attenuators */
            case rffegetatt:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_ATT, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set RFFE Temperature */
            case rffesettemp:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_TEMP_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Read RFFE Temperature */
            case rffegettemp:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_TEMP_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set RFFE Point */
            case rffesetpnt:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_SET_POINT_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get RFFE Point */
            case rffegetpnt:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_SET_POINT_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Set RFFE Temperature Control */
            case rffesettempctr:
                item.name = RFFE_NAME_SET_GET_TEMP_CONTROL;
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* Get RFFE Temperature Control */
            case rffegettempctr:
                item.name = RFFE_NAME_SET_GET_TEMP_CONTROL;
                item.service = RFFE_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

                /* Set RFFE Output */
            case rffesetout:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_HEATER_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* Get RFFE Output */
            case rffegetout:
                if ((err = parse_subopt (optarg, mount_opts, RFFE_NAME_SET_GET_HEATER_AC, corr_name, item.write_val)) != HALCS_CLIENT_SUCCESS) {
                    fprintf(stderr, "%s: %s - '%s'\n", program_name, halcs_client_err_str(err), corr_name);
                    exit(EXIT_FAILURE);
                }
                item.name = strdup(corr_name);
                item.service = RFFE_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                free(item.name);
                break;

                /* RFFE Reset */
            case rffereset:
                item.name = RFFE_NAME_SET_GET_RESET;
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /* RFFE Reprogram */
            case rfferpg:
                item.name = RFFE_NAME_SET_GET_REPROG;
                item.service = RFFE_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

                /******** ACQ Module Functions ********/

            case setacqtrig:
                item.name = ACQ_NAME_CFG_TRIG;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getacqtrig:
                item.name = ACQ_NAME_CFG_TRIG;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case setdatatrigchan:
                item.name = ACQ_NAME_HW_DATA_TRIG_CHAN;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getdatatrigchan:
                item.name = ACQ_NAME_HW_DATA_TRIG_CHAN;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case setdatatrigpol:
                item.name = ACQ_NAME_HW_DATA_TRIG_POL;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getdatatrigpol:
                item.name = ACQ_NAME_HW_DATA_TRIG_POL;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case setdatatrigsel:
                item.name = ACQ_NAME_HW_DATA_TRIG_SEL;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getdatatrigsel:
                item.name = ACQ_NAME_HW_DATA_TRIG_SEL;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case setdatatrigfilt:
                item.name = ACQ_NAME_HW_DATA_TRIG_FILT;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getdatatrigfilt:
                item.name = ACQ_NAME_HW_DATA_TRIG_FILT;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case setdatatrigthres:
                item.name = ACQ_NAME_HW_DATA_TRIG_THRES;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtol(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case getdatatrigthres:
                item.name = ACQ_NAME_HW_DATA_TRIG_THRES;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case settrigdly:
                item.name = ACQ_NAME_HW_TRIG_DLY;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = strtoul(optarg, NULL, 10);
                append_item (call_list, item);
                break;

            case gettrigdly:
                item.name = ACQ_NAME_HW_TRIG_DLY;
                item.service = ACQ_MODULE_NAME;
                item.rw = 1;
                *item.write_val = item.rw;
                append_item (call_list, item);
                break;

            case genswtrig:
                item.name = ACQ_NAME_SW_TRIG;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = 1; /* Generate trigger */
                append_item (call_list, item);
                break;

            case acqstop:
                item.name = ACQ_NAME_FSM_STOP;
                item.service = ACQ_MODULE_NAME;
                item.rw = 0;
                *item.write_val = item.rw;
                *(item.write_val+4) = 1; /* Generate stop event */
                append_item (call_list, item);
                break;

                /*  Set Acq Pre-trigger Samples */
            case setsamplespre:
                acq_samples_pre_val =  strtoul(optarg, NULL, 10);
                break;

            case setsamplespost:
                acq_samples_post_val =  strtoul(optarg, NULL, 10);
                break;

            case setnumshots:
                acq_num_shots_val =  strtoul(optarg, NULL, 10);
                break;

                /*  Set Acq Chan */
            case 'H':
                acq_chan_set = 1;
                acq_chan_val =  strtoul(optarg, NULL, 10);
                break;

                /*  Set Acq Start */
            case 'I':
                acq_start_call = 1;
                break;

                /*  Check if the acquisition is finished */
            case 'K':
                acq_check_call = 1;
                check_poll = 0;
                break;

                /*  Check if the acquisition is finished until timeout (-1 for infinite) */
            case acqcheckpoll:
                acq_check_call = 1;
                check_poll = 1;
                break;

                /*  Get a single data block from the acquisition */
            case 'A':
                acq_get_block = 1;
                acq_block_id =  strtoul(optarg, NULL, 10);
                break;

                /*  Get a whole data curve */
            case getcurve:
                acq_get_curve_call = 1;
                break;

                /*  Perform full acq */
            case fullacq:
                acq_full_call = 1;
                break;

                /*  Set Polling timeout */
            case timeout:
                poll_timeout = (int) strtol(optarg, NULL, 10);
                break;

                /*  Set acquisition output format */
            case filefmt:
                filefmt_str = strdup (optarg);
                break;

            default:
                fprintf(stderr, "%s: bad option\n", program_name);
                print_usage(program_name, stderr, 1);
        }
    }

    /* User input error handling */

    /* Use default local broker endpoint if none was given */
    if (broker_endp == NULL){
        broker_endp = strdup(default_broker_endp);
    }

    /* Check if the board number is within range and set to default if necessary */
    if (board_number_str == NULL) {
        fprintf (stderr, "[client]: Setting default value to BOARD number: %u\n",
                DFLT_BOARD_NUMBER);
        board_number = DFLT_BOARD_NUMBER;
    } else {
        board_number = strtoul (board_number_str, NULL, 10);
    }

    /* Check if the bpm number is within range and set to default if necessary */
    if (bpm_number_str == NULL) {
        fprintf (stderr, "[client]: Setting default value to BPM number: %u\n",
                DFLT_BPM_NUMBER);
        bpm_number = DFLT_BPM_NUMBER;
    } else {
        bpm_number = strtoul (bpm_number_str, NULL, 10);
    }

    if (acq_chan_set && (acq_chan_val >= END_CHAN_ID)) {
        fprintf(stderr, "%s: Invalid channel selected! This value must be lower than %u \n", program_name, END_CHAN_ID-1);
        exit(EXIT_FAILURE);
    }

    if ((acq_check_call && check_poll) && (poll_timeout == 0)) {
        fprintf(stderr, "%s: If --acqcheckpoll is set, --timeout must be too!\n", program_name);
        exit(EXIT_FAILURE);
    }

    if (acq_full_call && (acq_start_call || acq_check_call || acq_get_block || acq_get_curve_call)) {
        fprintf(stderr, "%s: If --fullacq is requested, the other acquisition functions don't need to be called. Executing --fullacq only...\n", program_name);
        acq_start_call = 0;
        acq_check_call = 0;
        acq_get_block = 0;
        acq_get_curve_call = 0;
    }

    /* Check filefmt option. filefmt has the default value of 0 (text mode) */
    if ((acq_full_call || acq_get_block || acq_get_curve_call) && filefmt_str != NULL) {
        filefmt_val = strtoul (filefmt_str, NULL, 10);

        if (filefmt_val > END_FILE_FMT-1) {
            fprintf (stderr, "[client:acq]: Invalid file format (--filefmt).\n");
            exit (EXIT_FAILURE);
        }
    }

    /* If we are here, all the parameters are good and the functions can be executed */
    halcs_client_t *halcs_client = halcs_client_new (broker_endp, verbose, NULL);
    acq_client_t *acq_client = acq_client_new (broker_endp, verbose, NULL);
    if (halcs_client == NULL || acq_client == NULL) {
        fprintf(stderr, "[client]: Error in memory allocation for halcs_client\n");
        exit(EXIT_FAILURE);
    }

    /* Call all functions from the FMC130M_4CH, SWAP and DSP Module that the user specified */
    call_func_t* function = (call_func_t *)zlist_first (call_list);

    for ( ; function != NULL; function = zlist_next (call_list))
    {
        int str_length = snprintf(NULL, 0, "HALCS%u:DEVIO:%s%u", board_number, function->service, bpm_number);
        char *func_service = zmalloc (str_length+1);
        sprintf (func_service, "HALCS%u:DEVIO:%s%u", board_number, function->service, bpm_number);
        const disp_op_t* func_structure = halcs_func_translate (function->name);
        halcs_client_err_e err = halcs_func_exec (halcs_client, func_structure, func_service, function->write_val, function->read_val);

        if (err != HALCS_CLIENT_SUCCESS) {
            fprintf (stderr, "[client]: %s\n",halcs_client_err_str (err));
            exit(EXIT_FAILURE);
        }

        if (func_structure->retval != DISP_ARG_END && function->rw) {
            print_func_v(1, function);
        }
        free (func_service);
    }
    zlist_destroy (&call_list);

    /***** Acquisition module routines *****/
    char acq_service[20];
    sprintf (acq_service, "HALCS%u:DEVIO:ACQ%u", board_number, bpm_number);

    /* Request data acquisition on server */
    acq_total_samples_val = (acq_samples_pre_val+acq_samples_post_val)*acq_num_shots_val;

    if (acq_start_call) {
    /* Wrap the data request parameters */
        acq_req_t acq_req = {
            .num_samples_pre = acq_samples_pre_val,
            .num_samples_post = acq_samples_post_val,
            .num_shots = acq_num_shots_val,
            .chan = acq_chan_val
        };

        halcs_client_err_e err = acq_start(acq_client, acq_service, &acq_req);
        if (err != HALCS_CLIENT_SUCCESS) {
            fprintf (stderr, "[client:acq]: '%s'\n", halcs_client_err_str(err));
            exit(EXIT_FAILURE);
        }
    }

    /* Check if the previous acquisition has finished */
    if (acq_check_call) {
        if (check_poll) {
            func_polling (halcs_client, ACQ_NAME_CHECK_DATA_ACQUIRE, acq_service, NULL, NULL, poll_timeout);
        } else {
            halcs_client_err_e err = acq_check(acq_client, acq_service);
            if (err != HALCS_CLIENT_SUCCESS) {
                fprintf (stderr, "[client:acq]: '%s'\n", halcs_client_err_str(err));
            }
        }
    }

    const acq_chan_t *acq_chan = acq_get_chan (acq_client);

    /* Retrieve specific data block */
    if (acq_get_block) {
        uint32_t data_size = acq_total_samples_val*acq_chan[acq_chan_val].sample_size;
        uint32_t *valid_data = (uint32_t *) zmalloc (data_size*sizeof (uint8_t));
        acq_trans_t acq_trans = {
            .req = {
                .chan = acq_chan_val,
            },
            .block = {
                .idx = acq_block_id,
                .data = valid_data,
                .data_size = data_size
            }
        };

        halcs_client_err_e err = acq_get_data_block (acq_client, acq_service, &acq_trans);

        if (err == HALCS_CLIENT_SUCCESS) {
            PRINTV (verbose, "[client:acq]: halcs_get_block was successfully executed\n");
            print_data_curve (acq_chan_val, acq_trans.block.data, acq_trans.block.bytes_read,
                    filefmt_val);
        } else {
            fprintf (stderr, "[client:acq]: halcs_get_block failed\n");
        }
        free(valid_data);
    }

    /* Returns a whole data curve */
    if (acq_get_curve_call) {
        uint32_t data_size = acq_total_samples_val*acq_chan[acq_chan_val].sample_size;
        uint32_t *valid_data = (uint32_t *) zmalloc (data_size*sizeof (uint8_t));

        acq_trans_t acq_trans = {
            .req = {
                .num_samples_pre = acq_samples_pre_val,
                .num_samples_post = acq_samples_post_val,
                .num_shots = acq_num_shots_val,
                .chan = acq_chan_val  },
            .block = {
                .data = valid_data,
                .data_size = data_size }
        };

        halcs_client_err_e err = acq_get_curve(acq_client, acq_service, &acq_trans);

        if (err == HALCS_CLIENT_SUCCESS) {
            print_data_curve (acq_chan_val, acq_trans.block.data, acq_trans.block.bytes_read,
                    filefmt_val);
            PRINTV (verbose, "[client:acq]: acq_get_curve was successfully executed\n");
        } else {
            fprintf (stderr, "[client:acq]: acq_get_curve failed: %s\n", halcs_client_err_str(err));
            exit(EXIT_FAILURE);
        }
        acq_get_curve_call = 0;
        free(valid_data);
    }

    /* Perform a full acquisition routine and return a data curve */
    if (acq_full_call) {
        uint32_t data_size = acq_total_samples_val*acq_chan[acq_chan_val].sample_size;
        uint32_t *valid_data = (uint32_t *) zmalloc (data_size*sizeof (uint8_t));

        acq_trans_t acq_trans = {
            .req = {
                .num_samples_pre = acq_samples_pre_val,
                .num_samples_post = acq_samples_post_val,
                .num_shots = acq_num_shots_val,
                .chan = acq_chan_val  },
            .block = {
                .data = valid_data,
                .data_size = data_size }
        };

        halcs_client_err_e err = acq_full(acq_client, acq_service, &acq_trans, poll_timeout);

        if (err != HALCS_CLIENT_SUCCESS) {
            fprintf (stderr, "[client:acq]: %s\n", halcs_client_err_str(err));
            exit(EXIT_FAILURE);
        }
        print_data_curve (acq_chan_val, acq_trans.block.data, acq_trans.block.bytes_read,
                filefmt_val);
        acq_full_call = 0;
        free(valid_data);
    }

    /* Deallocate memory */
    free (filefmt_str);
    free (default_broker_endp);
    free (broker_endp);
    free (board_number_str);
    free (bpm_number_str);
    halcs_client_destroy (&halcs_client);
    acq_client_destroy (&acq_client);
    return 0;
}
