
using System;
using System.Diagnostics.Metrics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Diagnostics;
using ftl_sim;
using Newtonsoft.Json.Linq;
using SimSharp;
using static System.Runtime.InteropServices.JavaScript.JSType;
using static SimSharp.Distributions;

using Environment = SimSharp.Environment;

public struct Param
{
    public uint MTS = 3600;  // Mega.
    public uint DATA_SIZE = 512 + 64; // Parity included.
    public uint NUM_CH = 4;
    public uint NUM_WAY = 32;
    public uint NUM_PLANE = 4;

    public uint DIEQ_LEN = 2; // Die queue length.

    public uint T_READ;
    public uint T_PGM; // Programming time in microseconds.
    public uint T_ERB; // Erase block time in milliseconds.

    public uint T_FW; // Firmware time in nanoseconds.
    public uint T_DMA; // DMA time in nanoseconds.
    public uint T_CMD; // Command time in nanoseconds.

    public bool EN_SCA = false;

    List<(string, JToken)> stConfig;
    string strCfgName;
    JObject stCommonCfg;
    public Param()
    {
        T_READ = US(10); // Read time in microseconds.
        T_PGM = US(150); // Programming time in microseconds
        T_ERB = MS(10); // Erase block time in milliseconds
        T_FW = NS(1); // Firmware time in nanoseconds.
        T_DMA = NS((DATA_SIZE * 1000) / MTS); // DMA for 512+64 (3600)
        T_CMD = NS(200); // Command time in nanoseconds.
        stConfig = new List<(string, JToken)>();
    }

    public UInt32 NS(UInt32 x) => x; // Convert nanoseconds to TimeSpan ticks.
    public UInt32 US(UInt32 x) => NS(x) * 1000; // Convert microseconds to nanoseconds.
    public UInt32 MS(UInt32 x) => US(x) * 1000; // Convert milliseconds to nanoseconds.
    public UInt32 SEC(UInt32 x) => MS(x) * 1000; // Convert seconds to nanoseconds.
    public TimeSpan T(UInt32 time) => TimeSpan.FromTicks(time);



    public void Save()
    {
        var json = new JObject();
        json["MTS"] = MTS;
        json["DATA_SIZE"] = DATA_SIZE;
        Console.WriteLine(json);
    }

    private void setupJson(string name, JObject jset)
    {
        strCfgName = name; // Store the configuration name.
        if (jset.ContainsKey("MTS")) MTS = (uint)jset["MTS"];
        if(jset.ContainsKey("NUM_WAY")) NUM_WAY = (uint)jset["NUM_WAY"];
        if(jset.ContainsKey("DATA_SIZE")) DATA_SIZE = (uint)jset["DATA_SIZE"];
        if(jset.ContainsKey("NUM_CH")) NUM_CH = (uint)jset["NUM_CH"];
        if(jset.ContainsKey("NUM_PLANE")) NUM_PLANE = (uint)jset["NUM_PLANE"];
        if(jset.ContainsKey("DIEQ_LEN")) DIEQ_LEN = (uint)jset["DIEQ_LEN"];
        if(jset.ContainsKey("T_READ")) T_READ = (uint)jset["T_READ"];
        if(jset.ContainsKey("T_PGM")) T_PGM = (uint)jset["T_PGM"];
        if(jset.ContainsKey("T_ERB")) T_ERB = (uint)jset["T_ERB"];
        if(jset.ContainsKey("T_FW")) T_FW = (uint)jset["T_FW"];
        if(jset.ContainsKey("T_DMA")) T_DMA = (uint)jset["T_DMA"];
        if(jset.ContainsKey("T_CMD")) T_CMD = (uint)jset["T_CMD"];
        if(jset.ContainsKey("EN_SCA")) EN_SCA = (bool)jset["EN_SCA"];
    }
    public bool Load(string file_name)
    {
        JObject j1 = JObject.Parse(File.ReadAllText(file_name)); // Load parameters from JSON file.
        foreach(var jset in j1)
        {
            Console.WriteLine($"SET: {jset}");
            if (jset.Key == "common")
            {
                stCommonCfg = (JObject)jset.Value; // Store the common configuration.
            }
            else
            {
                stConfig.Add((jset.Key, jset.Value));
                Console.WriteLine($"Lv2: {jset}");
            }
        }
        return true;
    }
    public bool NextSet()
    {
        if (stConfig.Count == 0)
        {
            return false; // No more configurations to process.
        }
        setupJson("common", stCommonCfg);
        var value = stConfig[0];
        stConfig.RemoveAt(0); // Remove the first element from the list.
        setupJson(value.Item1, (JObject)(value.Item2));
        return true;
    }
    public string GetName()
    {
        return strCfgName; // Return the current configuration name.
    }
}

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
}

public class Die
{
    uint nId;
    uint bmBusy;

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

        stPgBuf = new Resource[p.NUM_PLANE]; // Page buffer resource.
        stCell = new Resource[p.NUM_PLANE]; // Cell resource for operations.
        for(int i=0; i < p.NUM_PLANE; i++)
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

        Log($"{cmd.nSeq} Busy ");
        yield return stEnv.Timeout(stP.T(stP.T_READ));

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
        for(int i = 0; i < stCmdQ.Count; i++)
        {
            CmdInfo cmd = stCmdQ.ElementAt(i);
            if((bm_busy & cmd.bmPln) == 0)
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
            for (int way = 0; way < p.NUM_WAY; way++)
            {
                Die die = new Die((uint)(ch * p.NUM_WAY + way), env, p, data_ch, cmd_ch, cpl_q);
                dies.Add(die);
                die.Simulate();
            }
        }
        return dies;
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
    int nCntDie;
    List<Die> astDie;
    Random stRand;

    int nCntIssue;
    int nCntDone;

    public Tester(Environment env, Param p, List<Die> dies, Store cpl_q)
    {
        stEnv = env;
        stP = p;
        stCplQ = cpl_q;
        nCntDie = 0;
        stRand = new Random(22); // Initialize random number generator.
        astDie = dies;
        nCntDie = dies.Count;

        nCntIssue = 0;
        nCntDone = 0;
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
        while(true) // for (int i=0; i< 100; i++)
        {
            yield return stEnv.Timeout(stP.T(stP.T_FW)); // Simulate firmware processing time.
            int die_no = stRand.Next() % nCntDie; // Cycle through available dies.
            uint bm_plane = (uint)(1 << (stRand.Next() % (int)stP.NUM_PLANE));
            CmdInfo cmd = new CmdInfo
            {
                bmPln = bm_plane, // Assign a plane for the command.
                eOp = CmdInfo.OpCode.READ,
                nDie = (uint)die_no,
                nSeq = seq++
            }; // Example command, can be modified for testing.
            //yield return stEnv.Process(astDie[die_no].PutCmd(cmd));
            yield return stEnv.Process(astDie[die_no].PutCmd(cmd));
            nCntIssue++;
        }
    }

    private IEnumerable<Event> runCompl()
    {
        while(true)
        {
            var get = stCplQ.Get();
            yield return get;
            nCntDone++;
            CmdInfo cmd = (CmdInfo)get.Value;
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

class Program
{
    static void Main(string[] args)
    {
        Param p = new Param();
        p.Load("X:\\desktop\\ftl_sim\\param.json"); // Save parameters to JSON format.
        while (p.NextSet())
        {
#if false
            Console.WriteLine($"Run {p.GetName()}: {p.NUM_PLANE}");
#else
            Environment env = new Environment();
            Store cpl_q = new Store(env); // Completion queue for commands.
            List<Die> dies = Die.MakeDies(env, p, cpl_q); // Create SSD with dies.
            Tester test = new Tester(env, p, dies, cpl_q);

            TimeSpan run_time = p.T(p.MS(1));
            test.Simulate(run_time, run_time / 10);

            // Example simulation run.
            env.Run(run_time);
#endif
        }
    }
}
