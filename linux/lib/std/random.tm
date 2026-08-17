// std/random.tm — 擬似乱数生成 (Tenmu実装)
//
// アルゴリズム: xoshiro256** (Blackman & Vigna, 2018) をシード拡散に
// splitmix64 と組み合わせる。暗号用途には使わない、高速・高品質な
// 汎用PRNGとして広く実績のある組み合わせ。

module std.random

import std.core
import std.os

error RandomError {
    NoEntropySource,
}

struct Rng {
    state: [4]u64,
}

fn rotl(x: u64, k: u64) -> u64 {
    return (x << k) | (x >> (64 - k))
}

fn splitmix64_next(state: &mut u64) -> u64 {
    *state = *state + 0x9E3779B97F4A7C15
    let mut z = *state
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB
    return z ^ (z >> 31)
}

impl Rng {
    // 与えたシードから決定的に初期化する(再現可能な乱数列が必要な場合に使う)
    fn from_seed(seed: u64) -> Rng {
        let mut sm = seed
        let s0 = splitmix64_next(&mut sm)
        let s1 = splitmix64_next(&mut sm)
        let s2 = splitmix64_next(&mut sm)
        let s3 = splitmix64_next(&mut sm)
        return Rng { state: [s0, s1, s2, s3] }
    }

    // OSのエントロピー源からシードする(ホスト型ターゲットのみ)
    fn from_entropy() -> Result<Rng, RandomError> {
        let seed = os.getrandom_u64()?
        return Ok(Rng.from_seed(seed))
    }

    fn next_u64(&mut self) -> u64 {
        let result = rotl(self.state[1] * 5, 7) * 9
        let t = self.state[1] << 17

        self.state[2] = self.state[2] ^ self.state[0]
        self.state[3] = self.state[3] ^ self.state[1]
        self.state[1] = self.state[1] ^ self.state[2]
        self.state[0] = self.state[0] ^ self.state[3]
        self.state[2] = self.state[2] ^ t
        self.state[3] = rotl(self.state[3], 45)

        return result
    }

    fn next_u32(&mut self) -> u32 {
        return (self.next_u64() >> 32) as u32
    }

    // [0, 1) の一様乱数。倍精度の仮数部53bitに合わせて上位53bitを使う
    fn next_f64(&mut self) -> f64 {
        let bits = self.next_u64() >> 11
        return (bits as f64) * (1.0 / 9007199254740992.0) // 1 / 2^53
    }

    fn next_bool(&mut self) -> bool {
        return (self.next_u64() & 1) == 1
    }

    // [low, high) の範囲の整数を返す
    fn range_i64(&mut self, low: i64, high: i64) -> i64 {
        let span = (high - low) as u64
        return low + (self.next_u64() % span) as i64
    }

    // 平均mean、標準偏差stddevの正規分布(Box-Muller変換)
    fn gaussian(&mut self, mean: f64, stddev: f64) -> f64 {
        let u1 = self.next_f64()
        let u2 = self.next_f64()
        let r = core.sqrt(-2.0 * core.ln(u1))
        let theta = 2.0 * core.PI * u2
        return mean + stddev * r * core.cos(theta)
    }

    // Fisher-Yatesシャッフル(その場で並べ替える)
    fn shuffle<T>(&mut self, items: &mut []T) {
        let n = items.len()
        if n < 2 {
            return
        }
        let mut i = n - 1
        while i > 0 {
            let j = self.range_i64(0, (i + 1) as i64) as usize
            let tmp = items[i]
            items[i] = items[j]
            items[j] = tmp
            i = i - 1
        }
    }
}
