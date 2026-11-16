#pragma once

// Crash reporter: if the server crashes (segfault, abort, stack smashing, ...), writes the call stack to the console
// and to cfg/hns-movement/crash-<unix time>.txt before the game's own crash handler runs. Each line names the module
// and the offset inside it ("hns-movement.so(+0x1a2b3c)"), which addr2line turns into a file and line using the same
// build. Linux only; does nothing on Windows.
namespace hns::crashlog
{
	void Install();
	void Uninstall();
} // namespace hns::crashlog
