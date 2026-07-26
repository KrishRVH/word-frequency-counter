import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.4.10"
    id("dev.detekt") version "2.0.0-alpha.5"
    application
}

group = "dev.krvh.wordcount"
version = "0.1.0"

dependencyLocking {
    lockAllConfigurations()
}

kotlin {
    jvmToolchain(26)
}

application {
    applicationName = "wordcount-kotlin"
    mainClass.set("wordcount.MainKt")
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_26)
        allWarningsAsErrors.set(true)
        freeCompilerArgs.add("-Xjsr305=strict")
    }
}

detekt {
    allRules = true
    buildUponDefaultConfig = true
    basePath.set(projectDir)
    config.setFrom(files("detekt.yml"))
}

tasks.withType<dev.detekt.gradle.Detekt>().configureEach {
    jvmTarget.set("26")
    reports {
        checkstyle.required.set(true)
        html.required.set(false)
        markdown.required.set(false)
        sarif.required.set(false)
    }
}

tasks.withType<dev.detekt.gradle.DetektCreateBaselineTask>().configureEach {
    jvmTarget.set("26")
}
