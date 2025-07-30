
def NS(x) : 
    return x
def US(x):
    return x * NS(1000)
def MS(x):
    return x * US(1000)
def SEC(x):
    return x * MS(1000)

# NAND Timing parameter.
MTS = 3600  # Mega.
DATA_SIZE = 512 + 64   # Parity included.

T_CMD = NS(200)
T_DMA = NS(DATA_SIZE * 1000 / MTS) #NS(144)		# DMA for 512+8 (3600)

T_READ = US(10)
T_PROG = US(150)
T_BER = MS(10)

T_FW = NS(1)

# NAND matrix.
NUM_CH = 4
NUM_WAY = 32
NUM_PLANE = 4

# Queue config.
DIEQ_LEN = 2
CMDQ_LEN = 512 # DIEQ_LEN * NUM_CH * NUM_WAY * NUM_PLANE

# Features. 
EN_SCA = True

# Test config.
SIM_TIME = MS(3)
UNI_RAND = False
EN_MONITOR = False
EN_LOG = False
EN_LIMIT_DIEQ = False


A_QD = [512]
A_PIR = [4]
A_TR = [US(16)] # US(14.8+1.5), US(13.9+1.5), US(9.8+1.5), US(3+1)] #, npa.US(9), npa.US(8)]

def pn_print():
    for var_name, value in globals().items():
        if isinstance(value, int):
            print(f"{var_name}: {value}")
        elif isinstance(value, bool):
            print(f"{var_name}: {value}")
    
