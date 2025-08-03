using System;
using Newtonsoft.Json.Linq;


namespace ftl_sim
{
    public struct Param
    {
        public uint MTS = 3600;  // Mega.
        public uint DATA_SIZE = 512 + 64; // Parity included.
        public uint NUM_CH = 4;
        public uint NUM_WAY = 32;
        public uint NUM_PLANE = 4;

        public uint CMDQ_LEN = 8; // Command queue length

        public uint DIEQ_LEN = 2; // Die queue length.
        public uint NUM_BUF = 16; // Number of buffers.

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
            if (jset.ContainsKey("NUM_WAY")) NUM_WAY = (uint)jset["NUM_WAY"];
            if (jset.ContainsKey("DATA_SIZE")) DATA_SIZE = (uint)jset["DATA_SIZE"];
            if (jset.ContainsKey("NUM_CH")) NUM_CH = (uint)jset["NUM_CH"];
            if (jset.ContainsKey("NUM_PLANE")) NUM_PLANE = (uint)jset["NUM_PLANE"];
            if (jset.ContainsKey("DIEQ_LEN")) DIEQ_LEN = (uint)jset["DIEQ_LEN"];
            if (jset.ContainsKey("T_READ")) T_READ = (uint)jset["T_READ"];
            if (jset.ContainsKey("T_PGM")) T_PGM = (uint)jset["T_PGM"];
            if (jset.ContainsKey("T_ERB")) T_ERB = (uint)jset["T_ERB"];
            if (jset.ContainsKey("T_FW")) T_FW = (uint)jset["T_FW"];
            if (jset.ContainsKey("T_DMA")) T_DMA = (uint)jset["T_DMA"];
            if (jset.ContainsKey("T_CMD")) T_CMD = (uint)jset["T_CMD"];
            if (jset.ContainsKey("EN_SCA")) EN_SCA = (bool)jset["EN_SCA"];
        }
        public bool Load(string file_name)
        {
            JObject j1 = JObject.Parse(File.ReadAllText(file_name)); // Load parameters from JSON file.
            foreach (var jset in j1)
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
}
