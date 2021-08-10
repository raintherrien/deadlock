use async_std::task;
use std::time::{Duration, Instant};

#[async_std::main]
async fn main() {
	const ITER_COUNT: i64 = 8192;

	let mut avg_join_complete_lat_ns: i64 = 0;
	let mut avg_join_wait_lat_ns:     i64 = 0;
	let mut avg_spawn_lat_ns:         i64 = 0;

	for _ in 0..ITER_COUNT {
		let t0 = Instant::now();
		let t1 = task::spawn(async { return Instant::now(); }).await;
		let t2 = Instant::now();
		avg_join_complete_lat_ns += (t2 - t1).as_nanos() as i64;
		avg_spawn_lat_ns         += (t1 - t0).as_nanos() as i64;
	}

	for _ in 0..ITER_COUNT {
		let ftr = task::spawn(async {
			std::thread::sleep(Duration::from_millis(1));
			return Instant::now();
		});
		let t0 = ftr.await;
		let t1 = Instant::now();
		avg_join_wait_lat_ns += (t1 - t0).as_nanos() as i64;
	}

	avg_join_complete_lat_ns /= ITER_COUNT;
	avg_join_wait_lat_ns     /= ITER_COUNT;
	avg_spawn_lat_ns         /= ITER_COUNT;

	println!("Average latency of {} tasks:", ITER_COUNT);
	println!("\tjoin complete: {}ns", avg_join_complete_lat_ns);
	println!("\tjoin wait:     {}ns", avg_join_wait_lat_ns);
	println!("\tspawn:         {}ns", avg_spawn_lat_ns);
}
