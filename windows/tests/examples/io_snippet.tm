import std.io
import std.fs

fn load_and_log(config_path: str, log_path: str, allocator: &Allocator) -> Result<void, IoError> {
    let contents = match fs.read_to_string(config_path, allocator) {
        Ok(s) => s,
        Err(IoError.NotFound) => {
            io.eprintln("not found")
            return Err(IoError.NotFound)
        },
        Err(e) => return Err(e),
    }

    let log = fs.OpenOptions.new().write(true).create(true).append(true).open(log_path)?
    let mut writer = io.BufWriter.new(allocator, log)
    writer.write_all("wrote".as_bytes())?
    writer.flush()?
    return Ok(void)
}

fn list_tm_files(dir: str, allocator: &Allocator) -> Result<Vec<Path>, IoError> {
    let mut result = Vec.new(allocator)
    for entry in fs.read_dir(dir)? {
        let e = entry?
        if e.file_type == FileType.File && e.path.extension() == Some("tm") {
            result.push(e.path)
        }
    }
    return Ok(result)
}

fn compute() -> Result<i32, MathError> {
    try {
        let a = divide(10, 2)?
        let b = divide(a, 0)?
        return Ok(b)
    } catch (e) {
        io.println("error: #{e}")
        return Err(e)
    }
}
