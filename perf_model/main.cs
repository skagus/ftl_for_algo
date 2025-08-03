using SimSharp;

using Environment = SimSharp.Environment;

/**
 * 연결지향적인 프로세스 모델
 * 모든 모델링은 node와 edge로 구성된다.
 * 그리고 node에서는 필요한 리소스를 할당/사용하고 완료된 후에 edge를 통해 다음 노드로 이동한다.
 * Edge는 노드 간의 연결을 나타내며, queue로 표현된다.
 * 
 * */

namespace ftl_sim
{

	class Program
	{
		static void Main(string[] args)
		{
			Param p = new Param();
			p.Load("..\\..\\..\\fct_param.json"); // Save parameters to JSON format.
			while (p.NextSet())
			{
				Environment env = new Environment();
				Tester test = new Tester(env, p);

				TimeSpan run_time = p.T(p.MS(1));
				test.Simulate(run_time, run_time / 10);

				env.Run(run_time);
			}
		}
	}
}