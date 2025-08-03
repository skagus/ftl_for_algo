using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using SimSharp;

namespace temp
{
    public class Param
    {
        public TimeSpan NS(UInt32 x) => TimeSpan.FromTicks(x); // Convert nanoseconds to TimeSpan ticks.
        public TimeSpan US(UInt32 x) => NS(x * 1000); // Convert nanoseconds to TimeSpan ticks.
        public TimeSpan MS(UInt32 x) => US(x * 1000); // Convert nanoseconds to TimeSpan ticks.

        // NAND Timing parameter.
        public UInt32 MTS = 3600;  // Mega.
        public UInt32 DATA_SIZE = 512 + 64; //   # Parity included.

        public TimeSpan T_CMD;
        public TimeSpan T_DMA;
        public TimeSpan T_READ;
        public TimeSpan T_PROG;
        public TimeSpan T_BER;
        public TimeSpan T_FW;

        // NAND matrix.
        public UInt32 NUM_CH = 4;
        public UInt32 NUM_WAY = 32;
        public UInt32 NUM_PLANE = 4;

        // Queue config.
        public UInt32 DIEQ_LEN = 2;
        public UInt32 CMDQ_LEN = 512; // # DIEQ_LEN * NUM_CH * NUM_WAY * NUM_PLANE

        // Features. 
        public Boolean EN_SCA = true;

        // Test config.
        public TimeSpan SIM_TIME; // = MS(3)
        public Boolean UNI_RAND = false;
        public Boolean EN_MONITOR = false;
        public Boolean EN_LOG = false;
        public Boolean EN_LIMIT_DIEQ = false;
        Param()
        {
            T_CMD = NS(200);
            T_DMA = NS((DATA_SIZE * 1000) / MTS); // DMA for 512+8 (3600)
            T_READ = US(10);
            T_PROG = US(150);
            T_BER = MS(10);
            T_FW = NS(1);
            SIM_TIME = MS(3); // Default simulation time
        }
    }

    public enum OpCode
    {
        READ,
        PGM,
        ERB,
        NUM_OPCODES,
    }

    public enum PlnState
    {
        IDLE,
        R_CMD_W,
        R_CMD,
        R_BUSY,
        R_DMA_W,
        R_DMA,
    };


    // From param_nand.py
    public static class NandParameters
    {
        public const int PagesPerBlock = 256;
        public const int BlocksPerPlane = 1024;
        public const int PlanesPerDie = 4;
        public const int Dies = 2;

        public static readonly Dictionary<string, int> NandTimings = new Dictionary<string, int>
        {
            { "t_PROG", 1500 }, // us
            { "t_R", 100 },     // us
            { "t_BERS", 3500 }, // us
            { "t_DBSY", 1 },    // us
            { "t_CBSY", 1 }     // us
        };
    }

    struct NCmdInfo
    {
        public OpCode eOp;
        public UInt32 nDie;
        public UInt32 bmPln;
        public UInt32 nSeqNo;
        public NCmdInfo(OpCode eOp, UInt32 nDie, UInt32 bmPln, UInt32 nSeqNo)
        {
            this.eOp = eOp;
            this.nDie = nDie;
            this.bmPln = bmPln;
            this.nSeqNo = nSeqNo;
        }
    }

    class Die
    {
        SimSharp.Environment stEnv;
        UInt32 bmBusyPln;
        UInt32 nDieId;
        Resource stDataCh;
        Resource stCmdCh;
        List<NCmdInfo> stCmdQ;
        Event stIssueEvt;
        PlnState[] aeState;
        Store stCplQ;
        ///// Statistics /////
        UInt32[] aUtil;
        UInt32[] aPrvTime;
        TimeSpan stStatBase;
        Param stP;
        public Die(SimSharp.Environment env, Param p, UInt32 dieId, Resource data_ch, Resource cmd_ch, Store cpl_q)
        {
            stP = p;
            stEnv = env;
            nDieId = dieId;
            bmBusyPln = 0;
            stDataCh = data_ch;
            stCmdCh = cmd_ch;
            stCmdQ = new List<NCmdInfo>();
            stIssueEvt = new Event(env);
            stCplQ = cpl_q;
            aeState = new PlnState[p.NUM_PLANE];
            for (int i = 0; i < p.NUM_PLANE; i++)
            {
                aeState[i] = PlnState.IDLE;
            }
            aUtil = new UInt32[p.NUM_PLANE];
            aPrvTime = new UInt32[p.NUM_PLANE];
            stStatBase = TimeSpan.Zero;
        }

        public void SetState(NCmdInfo stCmd, PlnState state, PlnState prv_state)
        {
            for (Int32 i = 0; i < stP.NUM_PLANE; i++)
            {
                if ((stCmd.bmPln & (1 << i)) != 0)
                {
                    aeState[i] = state;
                    //aPrvTime[i] = stEnv.Now;
                }
            }
        }

        public IEnumerable<Event> ProcRead(NCmdInfo stCmd)
        {
            SetState(stCmd, PlnState.R_CMD_W, PlnState.IDLE);
            using (var req = stCmdCh.Request())
            {
                yield return req;
                SetState(stCmd, PlnState.R_CMD, PlnState.R_CMD_W);
                yield return stEnv.Timeout(stP.T_CMD);
            }
            SetState(stCmd, PlnState.R_BUSY, PlnState.R_CMD);
            yield return stEnv.Timeout(stP.T_READ);
            SetState(stCmd, PlnState.R_DMA_W, PlnState.R_BUSY);
            using (var req = stDataCh.Request())
            {
                yield return req;
                SetState(stCmd, PlnState.R_DMA, PlnState.R_DMA_W);
                yield return stEnv.Timeout(stP.T_DMA);
            }
            SetState(stCmd, PlnState.IDLE, PlnState.R_DMA);
            bmBusyPln &= ~stCmd.bmPln;
            stIssueEvt.Succeed();
            stIssueEvt = new Event(stEnv);

            yield return stCplQ.Put(stCmd);
        }

        public IEnumerable<Event> NewCmd(NCmdInfo stCmd)
        {
            if (stP.EN_LIMIT_DIEQ)
            {
                while (stCmdQ.Count() >= stP.DIEQ_LEN)
                {
                    yield return stIssueEvt;
                }
            }
            stCmdQ.Add(stCmd);
            stIssueEvt.Succeed();
            stIssueEvt = new Event(stEnv);
        }

        NCmdInfo? GetCmd()
        {
            UInt32 bmForbidden = bmBusyPln;
            for (int i = 0; i < stCmdQ.Count; i++)
            {
                if ((stCmdQ[i].bmPln & bmForbidden) == 0)
                {
                    NCmdInfo cmd = stCmdQ[i];
                    stCmdQ.RemoveAt(i);
                    return cmd;
                }
                else
                {
                    bmForbidden |= stCmdQ[i].bmPln;
                }
            }
            return null;
        }

        public IEnumerable<Event> Run()
        {
            while (true)
            {
                NCmdInfo? stCmd = GetCmd();
                if (stCmd.HasValue == false)
                {
                    yield return stIssueEvt;
                    continue;
                }
                stIssueEvt.Succeed();
                stIssueEvt = new Event(stEnv);

                if (stCmd.Value.eOp == OpCode.READ)
                {
                    bmBusyPln |= stCmd.Value.bmPln;
                    stEnv.Process(ProcRead(stCmd.Value));
                }
                else if (stCmd.Value.eOp == OpCode.PGM)
                {
                    // Handle programming operation here.
                    // For simplicity, we will not implement it in this example.
                    throw new NotImplementedException("Programming operation is not implemented.");
                }
                else if (stCmd.Value.eOp == OpCode.ERB)
                {
                    // Handle erase block operation here.
                    // For simplicity, we will not implement it in this example.
                    throw new NotImplementedException("Erase block operation is not implemented.");
                }
            }
        }
    }

    class Tester
    {
        public SimSharp.Environment env;
        public Store cpl_q;
        public SimSharp.Event cpl_evt;
        public UInt32 run_cnt;
        public UInt32 done_cnt;

        public Tester()
        {
            env = new SimSharp.Environment();
            cpl_evt = new SimSharp.Event(env);
            cpl_q = new Store(env, 100); // Initialize the completion queue with a capacity of 100.
        }

        public IEnumerable<Event> ProcCpl()
        {
            while (true)
            {
                var entry = cpl_q.Get();
                yield return entry;
                cpl_evt.Succeed();
                cpl_evt = new Event(env);
                done_cnt++;
                run_cnt--;
            }
        }
    }
}