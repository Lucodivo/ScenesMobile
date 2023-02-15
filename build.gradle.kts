// Top-level build file where you can add configuration options common to all sub-projects/modules.
buildscript {
    extra.set("kotlin_version", "1.6.10")

    repositories { // used for project dependencies below
        google()
        mavenCentral()
    }

    dependencies {
        val kotlin_version: String by rootProject.extra

        classpath("com.android.tools.build:gradle:7.2.0")
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:${kotlin_version}")
        classpath("com.google.gms:google-services:4.3.10")

        // Crashlytics plugin
        // NOTE: Do not place your application dependencies here; they belong
        // in the individual module build.gradle files
        classpath("com.google.firebase:firebase-crashlytics-gradle:2.8.1")

        // TODO: Hilt
        //classpath("com.google.dagger:hilt-android-gradle-plugin:2.40.1")
    }
}

allprojects { // used for dependencies in module gradles
    repositories {
        google()
        mavenCentral()
        maven("https://jitpack.io")
    }
}

tasks.register<Delete>("clean").configure {
    delete(rootProject.buildDir)
}