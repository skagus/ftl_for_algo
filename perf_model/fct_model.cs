
using System;
using System.Diagnostics;
using SimSharp;
using mylib;
using Environment = mylib.MyEnv;

namespace ftl_sim
{
     public struct CmdInfo
    {
        public enum OpCode
        {
            READ,
            PGM,
            ERB,
            NUM_OPCODES,
        }
        public OpCode eOp; // Operation code for the command.
        public uint nDie;
        public uint bmPln;
        public uint nSeq; // Sequence number for the command, can be used for tracking.
        public uint nCetId; // Number of buffers used for the command, if applicable.
        public uint nCntBuf; // Count of buffers used for the command, if applicable.
    }

    public class Die
    {
        uint nId;
        uint bmBusy;
        string vcdName;

        Param stP;
        LinkedList<CmdInfo> stCmdQ;

        Environment stEnv;
        Resource stDataCh;
        Resource stCmdCh;

        // for cache operation.
        Resource[] stPgBuf;
        Resource[] stCell;

        Store stCplQ;
        Event stNewIssue;

        public Die(uint id, Environment env, Param p, Resource data_ch, Resource cmd_ch, Store cpl_q)
        {
            stEnv = env;
            stP = p;
            nId = id;
            stCplQ = cpl_q;
            stDataCh = data_ch; // Channel resource for data transfer.
            stCmdCh = cmd_ch;
            bmBusy = 0;
            stNewIssue = new Event(env);
            stCmdQ = new LinkedList<CmdInfo>();

            string name = $"die_{id}";
            vcdName = env.VcdAddSignal(name, 1, 0);

            stPgBuf = new Resource[p.NUM_PLANE]; // Page buffer resource.
            stCell = new Resource[p.NUM_PLANE]; // Cell resource for operations.
            for (int i = 0; i < p.NUM_PLANE; i++)
            {
                stPgBuf[i] = new Resource(env, 1); // Initialize page buffer for each plane.
                stCell[i] = new Resource(env, 1); // Initialize cell resource for each plane.
            }
        }

        private void Log(string message)
        {
            uint tick = (uint)(stEnv.NowD * 10000000);
            Console.WriteLine($"{tick:n0}: {nId} {message}"); // Simple logging function.
        }

        private IEnumerable<Event> procRead(CmdInfo cmd)
        {
            using (var r = stCmdCh.Request()) // Request the data channel.
            {
                yield return r; // Wait for the channel to be available.
                Log($"{cmd.nSeq} {cmd.bmPln:X} Cmd Xfer.");
                yield return stEnv.Timeout(stP.T(stP.T_CMD));
            }

            stEnv.VcdUpdate(vcdName, 1);
            Log($"{cmd.nSeq} Busy ");
            yield return stEnv.Timeout(stP.T(stP.T_READ));
            stEnv.VcdUpdate(vcdName, 0);

            using (var req = stDataCh.Request()) // Request the data channel.
            {
                yield return req; // Wait for the channel to be available.
                Log($"{cmd.nSeq} Data Xfer.");
                yield return stEnv.Timeout(stP.T(stP.T_DMA));
            }

            // Simulate the operation of a plane.
            bmBusy &= ~cmd.bmPln; // Mark planes as free after operation.
            stNewIssue.Succeed(); // Notify that a new command has been added.
            stNewIssue = new Event(stEnv); // Reset the event for the next command.
            stCplQ.Put(cmd);
        }

        public IEnumerable<Event> PutCmd(CmdInfo cmd)
        {
            while (stCmdQ.Count >= stP.DIEQ_LEN)
            {
                Log("Command queue is full, waiting for completion.");
                yield return stNewIssue;
            }
            stCmdQ.AddLast(cmd);
            stNewIssue.Succeed(); // Notify that a new command has been added.
            stNewIssue = new Event(stEnv); // Reset the event for the next command.
            Log($"Command to die: {cmd.nDie}({cmd.nSeq})");
        }

        private CmdInfo? scanCmd()
        {
            int len = stCmdQ.Count;
            uint bm_busy = bmBusy;
            for (int i = 0; i < stCmdQ.Count; i++)
            {
                CmdInfo cmd = stCmdQ.ElementAt(i);
                if ((bm_busy & cmd.bmPln) == 0)
                {
                    stCmdQ.Remove(cmd);
                    return cmd;
                }
                bm_busy |= cmd.bmPln; // Mark planes as busy.
            }
            return null;
        }

        private IEnumerable<Event> run()
        {
            while (true)
            {
                while (stCmdQ.Count == 0)
                {
                    yield return stNewIssue; // Wait for a new command to be added.
                }
                CmdInfo? _cmd = scanCmd();
                if (_cmd.HasValue)
                {
                    CmdInfo cmd = _cmd.Value; // Get the command to process.
                    if (cmd.eOp == CmdInfo.OpCode.READ)
                    {
                        bmBusy |= cmd.bmPln; // Mark planes as busy.
                        stEnv.Process(procRead(cmd));
                        stNewIssue.Succeed();
                        stNewIssue = new Event(stEnv);
                    }
                    else
                    {
                        // TODO: Support Other operations like PGM, ERB.
                        Debug.Assert(false);
                    }
                }
                else
                {
                    yield return stNewIssue; // Wait for a new command to be added.
                }
            }
        }


        public void Simulate()
        {
            stEnv.Process(run());
        }
    }

    class Tester
    {
        Environment stEnv;
        Param stP;
        Store stCplQ;
        Store stCetPool;
        Container stBufPool;

        int nCntDie;
        List<Die> astDie;
        Random stRand;

        int nCntIssue;
        int nCntDone;

        public static List<Die> MakeDies(Environment env, Param p, Store cpl_q)
        {
            List<Die> dies = new List<Die>();
            for (int ch = 0; ch < p.NUM_CH; ch++)
            {
                Resource data_ch = new Resource(env, 1); // Channel resource for data transfer.
                Resource cmd_ch = data_ch;
                if (p.EN_SCA)
                {
                    cmd_ch = new Resource(env, 1); // Channel resource for command transfer.
                }
                env.VcdOpenModule($"ch_{ch}");
                for (int way = 0; way < p.NUM_WAY; way++)
                {
                    Die die = new Die((uint)(ch * p.NUM_WAY + way), env, p, data_ch, cmd_ch, cpl_q);

                    dies.Add(die);
                    die.Simulate();
                }
                env.VcdCloseModule();
            }
            return dies;
        }

        public Tester(Environment env, Param p)
        {
            stEnv = env;
            stP = p;
            stCplQ = new Store(env);
            stCetPool = new Store(env);
            stBufPool = new Container(env, p.NUM_BUF, p.NUM_BUF);
            for (uint i = 0; i < p.CMDQ_LEN; i++)
            {
                stCetPool.Put(i); // Initialize the buffer pool with a single buffer.
            }

            stRand = new Random(22); // Initialize random number generator.
            nCntIssue = 0;
            nCntDone = 0;

            astDie = MakeDies(env, p, stCplQ); // Create dies with the given parameters.
            nCntDie = astDie.Count;
        }

        private void Log(string message)
        {
            uint tick = (uint)(stEnv.NowD * 10000000);
            Console.WriteLine($"{tick:n0}: {message}"); // Simple logging function.
        }

        private IEnumerable<Event> performance(TimeSpan test_time, TimeSpan rampup)
        {
            yield return stEnv.Timeout(rampup); // Wait for ramp-up time.
            Log($"Performance test started");
            int prv_done = nCntDone;
            yield return stEnv.Timeout(test_time - TimeSpan.FromTicks(1));
            int done_count = nCntDone - prv_done;
            Log($"Performance test completed: {done_count} commands completed in {test_time / TimeSpan.NanosecondsPerTick} Sec.");
        }

        private IEnumerable<Event> runIssue()
        {
            uint seq = 0;
            while (true) // for (int i=0; i< 100; i++)
            {
                uint num_buf = 1;
                var get = stCetPool.Get(); // Request a buffer from the pool.
                yield return get; // Wait for the buffer to be available.
                uint cet_id = (uint)get.Value;
                var buf_get = stBufPool.Get(num_buf); // Get a buffer from the pool.
                yield return buf_get;

                yield return stEnv.Timeout(stP.T(stP.T_FW)); // Simulate firmware processing time.
                int die_no = stRand.Next() % nCntDie; // Cycle through available dies.
                uint bm_plane = (uint)(1 << (stRand.Next() % (int)stP.NUM_PLANE));
                CmdInfo cmd = new CmdInfo
                {
                    bmPln = bm_plane, // Assign a plane for the command.
                    eOp = CmdInfo.OpCode.READ,
                    nDie = (uint)die_no,
                    nSeq = seq++,
                    nCetId = cet_id,
                    nCntBuf = num_buf,
                }; // Example command, can be modified for testing.
                //yield return stEnv.Process(astDie[die_no].PutCmd(cmd));
                yield return stEnv.Process(astDie[die_no].PutCmd(cmd));
                nCntIssue++;
            }
        }

        private IEnumerable<Event> runCompl()
        {
            while (true)
            {
                var get = stCplQ.Get();
                yield return get;
                nCntDone++;

                CmdInfo cmd = (CmdInfo)get.Value;
                stCetPool.Put(cmd.nCetId); // Release the buffer back to the pool.
                stBufPool.Put(cmd.nCntBuf); // Release the command execution thread back to the pool.
                Log($"Command completed: {cmd.nSeq} {nCntDone}/{nCntIssue}");
            }
        }

        public void Simulate(TimeSpan total_time, TimeSpan rampup_time)
        {
            stEnv.Process(runIssue());
            stEnv.Process(runCompl());
            stEnv.Process(performance(total_time - rampup_time, rampup_time));
        }
    }
}

