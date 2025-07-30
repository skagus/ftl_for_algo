import simpy
import param_nand as npa
import simlib as sl
import random
import math

env = None
done_cnt = 0
run_cnt = 0
cpl_evt = None
seq = 0
sim_run = True
log = None
TOTAL_PIR = 0

# 소문자는 Idle 상태. 대문자는 Busy상태임.
ST_IDLE = "i_"
ST_R_CMD_W = "rc"
ST_R_CMD = "RC"
ST_R_BUSY = "RB"
ST_R_DMA_W = "rd"
ST_R_DMA = "RD"

ST_E_CMD_W = "ec"
ST_E_CMD = "EC"
ST_E_BUSY = "EB"

ST_P_CMD_W = "pc"
ST_P_CMD = "PC"
ST_P_DMA_W = "pd"
ST_P_DMA = "PD"
ST_P_BUSY = "PB"



def real_log(*args, **kwargs):
	print(env.now, *args, **kwargs)

def dummy_log(*args, **kwargs):
	pass

# 10% 범위의 random값.
def get_val_10(val):
	v = val / 10
	return val + random.randrange(-val, val+1)

def get_random(base, range):
	global seq
	if npa.UNI_RAND:
		seq += 1
		return base + (seq % range)
	else:
		return random.randint(base, base + range - 1)

class NCmd:
	def __init__(self, cmd, die_id, bm_plane, seq_no):
		self.cmd = cmd
		self.die_id = die_id
		self.bm_plane = bm_plane
		self.seq_no = seq_no
	def __str__(self):
		return f"C:{self.cmd} D:{self.die_id} P:{self.bm_plane:04b} ${self.seq_no}"


class Die:
	def __init__(self, env, die_id, data_ch, cmd_ch, cpl_q):
		self.stEnv = env
		self.bmBusyPln = 0		# Busy 상태인 plane.
		self.nDieId = die_id
		self.stDataCh = data_ch
		self.stCmdCh = cmd_ch
		self.stCmdQ = []
		self.stIssueEvt = env.event()  # Issue 가능상태가 된 경우.
		self.aeState = [ST_IDLE for i in range(npa.NUM_PLANE)] # plane별 현재 operation.
		self.stCplQ = cpl_q  # Completion queue.
		# Utilization
		self.aUtil = [0 for i in range(npa.NUM_PLANE)] # plane별 현재 operation.
		self.aPrvTime = [0 for i in range(npa.NUM_PLANE)] # plane별 현재 operation.
		self.statBase = 0
		env.process(self.run())

	# Plane별 state 변경 + State check.
	# Read(PIR)지원하지 않더라도, full plane으로 설정하면 ERB, PGM에도 문제 없음.
	# Plane단위의 PGM, ERB의 경우에도 plane interleave 지원가능.
	# 주의: Plane 단위 OP에서는 plane interleave가 된다는 점...
	def set_state(self, cmd, state, prv_state):
		log(f"ST: {prv_state} -> {state} {cmd}")
		for p in range(npa.NUM_PLANE):
			if(cmd.bm_plane & (1<<p)):
				assert(prv_state == self.aeState[p])
				prvBusy = (prv_state.upper() == prv_state)
				if prvBusy == True:
					self.aUtil[p] += env.now - self.aPrvTime[p]

				self.aeState[p] = state
				self.aPrvTime[p] = env.now

	def get_util(self, b_reset):
		period = env.now - self.statBase
		ret = [(n / period) for n in self.aUtil]
		if b_reset:
			self.statBase = env.now
			for i in range(len(self.aUtil)):
				self.aUtil[i] = 0
		return ret


	# Erase 진행. 
	# plane 단위 지원.
	def proc_erase(self, stCmd):
		assert(stCmd.bm_plane == (self.bmBusyPln & stCmd.bm_plane))
		self.set_state(stCmd, ST_E_CMD_W, ST_IDLE)
		with self.stCmdCh.request(priority = 2) as req:
			yield req
			self.set_state(stCmd, ST_E_CMD, ST_E_CMD_W)
			yield self.stEnv.timeout(npa.T_CMD)
		self.set_state(stCmd, ST_E_BUSY, ST_E_CMD)
		yield self.stEnv.timeout(npa.T_BER)
		self.set_state(stCmd, ST_IDLE, ST_E_BUSY)

		self.bmBusyPln = self.bmBusyPln & (~stCmd.bm_plane)
		self.stIssueEvt.succeed(0)
		self.stIssueEvt = env.event()
		yield self.stCplQ.put(stCmd)

	# program 처리.
	def proc_pgm(self, stCmd):
		assert(stCmd.bm_plane == (self.bmBusyPln & stCmd.bm_plane))
		self.set_state(stCmd, ST_P_CMD_W, ST_IDLE)
		with self.stCmdCh.request(priority = 2) as req:
			yield req
			self.set_state(stCmd, ST_P_CMD, ST_P_CMD_W)
			yield self.stEnv.timeout(npa.T_CMD)
		self.set_state(stCmd, ST_P_DMA_W, ST_P_CMD)
		with self.stDataCh.request(priority = 2) as req:
			yield req
			self.set_state(stCmd, ST_P_DMA, ST_P_DMA_W)
			yield self.stEnv.timeout(npa.T_DMA)
		self.set_state(stCmd, ST_P_BUSY, ST_P_DMA)
		yield self.stEnv.timeout(npa.T_PROG)
		self.set_state(stCmd, ST_IDLE, ST_P_BUSY)

		self.bmBusyPln = self.bmBusyPln & (~stCmd.bm_plane)
		self.stIssueEvt.succeed(0)
		self.stIssueEvt = env.event()
		yield self.stCplQ.put(stCmd)

	# Read 처리.
	def proc_read(self, stCmd):
		assert(stCmd.bm_plane == (self.bmBusyPln & stCmd.bm_plane))
		self.set_state(stCmd, ST_R_CMD_W, ST_IDLE)
		with self.stCmdCh.request() as req:
			yield req
			self.set_state(stCmd, ST_R_CMD, ST_R_CMD_W)
			yield self.stEnv.timeout(npa.T_CMD)
		self.set_state(stCmd, ST_R_BUSY, ST_R_CMD)
		#tR = get_val_10(npa.T_READ)
		tR = npa.T_READ
		yield self.stEnv.timeout(tR)
		self.set_state(stCmd, ST_R_DMA_W, ST_R_BUSY)
		with self.stDataCh.request(priority = 2) as req:
			yield req
			self.set_state(stCmd, ST_R_DMA, ST_R_DMA_W)
			yield self.stEnv.timeout(npa.T_DMA)
		self.set_state(stCmd, ST_IDLE, ST_R_DMA)

		self.bmBusyPln = self.bmBusyPln & (~stCmd.bm_plane)
		self.stIssueEvt.succeed(0)
		self.stIssueEvt = env.event()
		yield self.stCplQ.put(stCmd)

	def new_cmd(self, cmd):
		if npa.EN_LIMIT_DIEQ:
			while len(self.stCmdQ) >=  npa.DIEQ_LEN :
				yield self.stIssueEvt
		self.stCmdQ.append(cmd)
		self.stIssueEvt.succeed(0)
		self.stIssueEvt = env.event()
		log(f"Issue Cmd: {cmd}")
		#yield self.stCmdQ.put(cmd)

	def get_command(self):
		bmForbidden = self.bmBusyPln
		for idx in range(len(self.stCmdQ)):
			cmd = self.stCmdQ[idx]
			if 0 == (bmForbidden & cmd.bm_plane) :
				self.stCmdQ.pop(idx)
				return cmd
			else:
				bmForbidden |= cmd.bm_plane  # 같은 plane에 대해서, re-ordering은 금지.
		return None

	def run(self):  # generator.
		while True:
			#while len(self.stCmdQ) <= 0:
			#	yield self.stIssueEvt

			stCmd = self.get_command()
			if None == stCmd :
				yield self.stIssueEvt
			else:
				self.stIssueEvt.succeed(0)
				self.stIssueEvt = env.event()

				# stCmd = yield self.stCmdQ.get()
				if "program" == stCmd.cmd:
					self.bmBusyPln |= stCmd.bm_plane
					self.stEnv.process(self.proc_pgm(stCmd))
				elif "erase" == stCmd.cmd:
					self.bmBusyPln |= stCmd.bm_plane
					self.stEnv.process(self.proc_erase(stCmd))
				elif "read" == stCmd.cmd:
					# Plane busy를 proc_read에서 하면, 여러개가 동시에 시작됨.
					self.bmBusyPln |= stCmd.bm_plane
					self.stEnv.process(self.proc_read(stCmd))
				else:
					assert(0)

# 주기적으로 die/plane의 상태를 보여준다. 
def monitor(env, aDies, period):
	global sim_run
	while(True):
		yield env.timeout(period)
		if False == sim_run:
			break

		print(f"{env.now}: Busy state", end="") 
		cnt_run = 0
		for id in range(len(aDies)):
			if 0 == id % npa.NUM_WAY:
				print(f"\nCh:{int(id/npa.NUM_WAY):2d} ", end="")
			print(f"{aDies[id].aeState}{len(aDies[id].stCmdQ):2d} ", end="")
			for p in range(npa.NUM_PLANE):
				if aDies[id].aeState[p] != ST_IDLE:
					cnt_run += 1
		print(f"\nRunning Ratio = {cnt_run/TOTAL_PIR:.2f} = {cnt_run} / {TOTAL_PIR}")


# 10% 시간을 load 시간으로 감안하여, 후반 90% 시간동안 성능 측정.
def perf_check(env, total_test_time):
	global done_cnt
	
	lampup_time = total_test_time / 10
	check_time = total_test_time - lampup_time - 1
	yield env.timeout(lampup_time)
	
	prv_cnt = done_cnt
	yield env.timeout(check_time) 
	cnt = done_cnt - prv_cnt
	print(f"@ {cnt * npa.US(1) / check_time : .1f} MIOPs")


# Completion queue 처리.
def proc_cpl(env, cpl_q):
	global done_cnt
	global cpl_evt
	global run_cnt
	while(True):
		entry = yield cpl_q.get()
		done_cnt += 1
		run_cnt -= 1
		cpl_evt.succeed()
		cpl_evt = env.event()
		log(f"Cpl {entry}")

################## Test Scenario ###################
def TC_RR(env, aDies, max_run):
	global done_cnt
	global cpl_evt
	global run_cnt
	global sim_run

	if npa.EN_SCA :
		data_ch_max_miops = npa.NUM_CH * npa.US(1) / (npa.T_DMA)
		sca_ch_max_miops = npa.NUM_CH * npa.US(1) / (npa.T_CMD)
		print(f"C_Ch Max MIOPs: {sca_ch_max_miops:,.2f}")
	else:
		data_ch_max_miops = npa.NUM_CH * npa.SEC(1) / (npa.T_CMD + npa.T_DMA)
	
	die_max_miops = TOTAL_PIR * npa.US(1) / (npa.T_CMD + npa.T_DMA + npa.T_READ)
	print(f"N_Ch Max MIOPs: {data_ch_max_miops:,.2f}")
	print(f"NAND Max MIOPs: {die_max_miops:,.2f}")	
	est_util = 1 - math.exp(-npa.CMDQ_LEN / TOTAL_PIR)
	print(f"@ EST NAND MIOPs: {die_max_miops * est_util:,.2f} ({est_util*100:.1f}%)")

	seq_no = 0
	die_cnt = len(aDies)
	cmds = ["read"]

	# Issue command.
	while (True):
		if max_run <= run_cnt:
			yield cpl_evt

		yield env.timeout(npa.T_FW)
		rnd = get_random(0, die_cnt * npa.NUM_PLANE)
		die_id = int(rnd / npa.NUM_PLANE)
		bm_pln = 1 << int(rnd % npa.NUM_PLANE)
		cmd = random.choice(cmds)
		yield env.process(aDies[die_id].new_cmd(NCmd(cmd, die_id, bm_pln, seq_no)))
		run_cnt += 1
		seq_no += 1


def TC_Rand(env, aDies, max_run):
	global done_cnt
	global cpl_evt
	global run_cnt
	global sim_run
	seq_no = 0
	die_cnt = len(aDies)
	cmds = ["read", "program", "erase"]
	start_tick = env.now
	# Issue command.
	while True:
		if max_run <= run_cnt:
			yield cpl_evt

		yield env.timeout(npa.T_FW)
		rnd = get_random(0, die_cnt * npa.NUM_PLANE)
		die_id = int(rnd / npa.NUM_PLANE)
		bm_pln = 1 << int(rnd % npa.NUM_PLANE)
		cmd = random.choice(cmds)
		yield env.process(aDies[die_id].new_cmd(NCmd(cmd, die_id, bm_pln, seq_no)))
		run_cnt += 1
		seq_no += 1

def make_layout(env, num_ch, num_way, cpl_queue):
	dies = []
	data_chs = []
	cmd_chs = []
	for c in range(num_ch):
		#data_ch = simpy.PriorityResource(env, capacity=1) # num_way*npa.NUM_PLANE)
		data_ch = sl.OnePrioResource(env, capacity=1) # num_way*npa.NUM_PLANE)
		data_chs.append(data_ch)
		if npa.EN_SCA :
			cmd_ch = sl.OnePrioResource(env, capacity=1)
			#cmd_ch = simpy.PriorityResource(env, capacity=1)
		else:
			cmd_ch = data_ch
		cmd_chs.append(cmd_ch)

		for n in range(num_way):
			dies.append(Die(env, c * 100 + n, data_ch, cmd_ch, cpl_queue))
	return data_chs, cmd_chs, dies

def dump_stats(cChs, dChs, aDies):
	print("===== Utilization ==========")
	dChUtil = 0
	for dch in dChs:
		#print(f"Data Ch {dch.get_util(False):.4f}")
		dChUtil += dch.get_util(False)
	dChUtil /= len(dChs)

	cChUtil = 0
	for cch in cChs:
		#print(f"Cmd  Ch {cch.get_util(False):.4f}")
		cChUtil += cch.get_util(False)
	cChUtil /= len(cChs)

	plnUtil = 0
	cntPln = 0
	for die in aDies:
		plns_util = die.get_util(False)
		#print(f"Die Util {plns_util}")
		for util in plns_util:
			plnUtil += util
		cntPln += len(plns_util)
	plnUtil /= cntPln
	print(f"dCh:{dChUtil*100:.2f}%, cCh:{cChUtil*100:.2f}%, PLN:{plnUtil*100:.2f}%")


def nand_test_main():
	global env
	global done_cnt
	global run_cnt
	global cpl_evt
	global seq
	global sim_run

	env = simpy.Environment()
	done_cnt = 0
	run_cnt = 0
	cpl_evt = env.event()
	seq = 0
	sim_run = True
	random.seed(23)

	cpl_queue = simpy.Store(env, capacity=1)
	dChs, cChs, aDies = make_layout(env, npa.NUM_CH, npa.NUM_WAY, cpl_queue)
	env.process(proc_cpl(env, cpl_queue))
	env.process(TC_RR(env, aDies, npa.CMDQ_LEN))
	env.process(perf_check(env, npa.SIM_TIME))
	#env.process(random_io(env, aDies, npa.CMDQ_LEN, npa.NUM_ISSUE))
	if npa.EN_MONITOR:
		env.process(monitor(env, aDies, npa.US(10)))
	env.run(until = npa.SIM_TIME)
	dump_stats(cChs, dChs, aDies)


def configure():
	global log
	global TOTAL_PIR

	TOTAL_PIR = npa.NUM_CH * npa.NUM_WAY * npa.NUM_PLANE
	#MAX_RUN = TOTAL_PIR * npa.DIEQ_LEN  # Max User Issue.
	
	if npa.EN_LOG:
		log = real_log
	else:
		log = dummy_log

	#npa.pn_print()
	print(f"Layout: Ch {npa.NUM_CH} * W {npa.NUM_WAY} * P {npa.NUM_PLANE} = Total PIR: {TOTAL_PIR:,d}")
	print(f"T_CMD: {npa.T_CMD/npa.NS(1):.1f} nS, T_DMA:{npa.T_DMA/npa.NS(1):.1f}")
	print(f"T_READ: {npa.T_READ/npa.US(1)} uS, T_FW: {npa.T_FW/npa.NS(1)} nS")
	print(f"Cmd QD: {npa.CMDQ_LEN:,d}, DieQ Len: {npa.DIEQ_LEN}")
	print(f"@ CFG: CQ: {npa.CMDQ_LEN:d} tR: {npa.T_READ/npa.US(1):2.1f} tFW: {npa.T_FW/npa.NS(1):2.0f} ns T_PIR: {TOTAL_PIR:4d}")
	
def sweep(aQD, aPLN, aT_READ, aT_FW):
	for depth in aQD:
		npa.CMDQ_LEN = depth
		for pln in aPLN:
			npa.NUM_PLANE = pln
			for tr in aT_READ:
				npa.T_READ = tr
				configure()
				nand_test_main()

sweep(npa.A_QD, npa.A_PIR, npa.A_TR, npa.T_FW)
