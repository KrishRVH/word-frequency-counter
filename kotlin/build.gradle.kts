plugins {
    kotlin("jvm") version "2.4.0"
    id("io.gitlab.arturbosch.detekt") version "1.23.8"
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
        allWarningsAsErrors.set(true)
        freeCompilerArgs.add("-Xjsr305=strict")
    }
}

detekt {
    allRules = true
    buildUponDefaultConfig = true
}

tasks.withType<io.gitlab.arturbosch.detekt.Detekt>().configureEach {
    jdkHome.set(file(System.getProperty("java.home")))
    jvmTarget = "22"
    reports {
        html.required.set(false)
        md.required.set(false)
        sarif.required.set(false)
        txt.required.set(true)
        xml.required.set(false)
    }
}
