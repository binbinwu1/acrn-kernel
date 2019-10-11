Recompile the kernel to build-in pmu_generic to kernel.

pmu_user_sample.c provide the sample code, which can be reused in user application.
Typical Usage of the APIs in pmu_user_sample.c

benchmark_function () {

	pmu_start(level);

	loop ()
	{

		pmu_read_start();

		// code you want to profile
		// ...

		pmu_read_stop();

		pmu_print();
	}

	pmu_stop();
}