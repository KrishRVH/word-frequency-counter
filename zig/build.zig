const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const module = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = b.standardOptimizeOption(.{}),
    });
    const executable = b.addExecutable(.{
        .name = "wordcount_zig",
        .root_module = module,
    });
    b.installArtifact(executable);

    const check = b.addExecutable(.{
        .name = "wordcount_zig",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = .Debug,
        }),
    });
    const check_step = b.step("check", "Compile without emitting an executable");
    check_step.dependOn(&check.step);
}
