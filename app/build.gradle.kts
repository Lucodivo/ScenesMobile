plugins {
    id("com.android.application")
    id("kotlin-android")
    id("kotlin-android-extensions")
    id("com.google.firebase.crashlytics")
    id("com.google.gms.google-services")

    // TODO: Hilt
//    id("kotlin-kapt")
//    id("dagger.hilt.android.plugin") - uncomment to bring Hilt back into project
}

android {
    compileSdkVersion(31)
    defaultConfig {
        applicationId = "com.inasweaterpoorlyknit.learnopengl_androidport"
        minSdkVersion(24)
        targetSdkVersion(31)
        versionCode = 12
        versionName = "1.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17")
            }
        }

        compileOptions {
            sourceCompatibility(JavaVersion.VERSION_1_8)
            targetCompatibility(JavaVersion.VERSION_1_8)
        }

        buildFeatures {
            // Enables Jetpack Compose for this module
            compose = true
            viewBinding = true
        }

        composeOptions {
            kotlinCompilerExtensionVersion = "1.1.1"
        }

        packagingOptions {
            resources {
                excludes += listOf(
                        "META-INF/DEPENDENCIES",
                        "META-INF/LICENSE",
                        "META-INF/LICENSE.txt",
                        "META-INF/license.txt",
                        "META-INF/NOTICE",
                        "META-INF/NOTICE.txt",
                        "META-INF/notice.txt",
                        "META-INF/ASL2.0",
                        "META-INF/build.kotlin_module"
                )
            }
        }

        namespace = "com.inasweaterpoorlyknit.learnopengl_androidport"
    }

    buildTypes {
        named("release") {
            // Enables code shrinking, obfuscation, and optimization
            isMinifyEnabled = true
            // Enables resource shrinking
            isShrinkResources = true
            // Includes the default ProGuard rules files that are packaged with the Android Gradle plugin
            setProguardFiles(listOf(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro"))
        }
        named("debug") {
            isMinifyEnabled = false
            setProguardFiles(listOf(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro"))
        }
    }

    externalNativeBuild {
        cmake {
            path("src/main/cpp/CMakeLists.txt")
            version = "3.18.1"
        }
    }
}

dependencies {
    val kotlin_version: String by rootProject.extra

    // all binary .jar dependencies in libs folder listOf(NONE CURRENTLY)
//    implementation fileTree(mapOf("dir" to "libs", "include" to listOf("*.jar")))

    // Kotlin
    implementation("org.jetbrains.kotlin:kotlin-stdlib-jdk7:${kotlin_version}")

    // Google
    implementation("androidx.appcompat:appcompat:1.4.1")
    implementation("com.google.android.material:material:1.6.0")
    implementation("androidx.legacy:legacy-support-v4:1.0.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.3")
    implementation("androidx.preference:preference-ktx:1.1.1")
    implementation("androidx.fragment:fragment-ktx:1.4.1")


    // Compose
    implementation("androidx.activity:activity-compose:1.4.0") // Integration with activities
    implementation("androidx.compose.material:material:1.1.1") // Compose Material Design
    implementation("androidx.compose.material:material-icons-extended:1.1.1")
    implementation("androidx.compose.animation:animation:1.1.1") // Animations
    implementation("androidx.compose.ui:ui-tooling:1.1.1") // Tooling support (Previews, etc.)
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.4.1") // Integration with ViewModels
    //androidTestImplementation("androidx.compose.ui:ui-test-junit4:1.1.1") // UI Tests

    // GLM (OpenGL Mathematics)
    implementation("com.github.kotlin-graphics:glm:1.0.1")

    // analytics
    implementation("com.google.firebase:firebase-analytics:21.0.0")
    implementation("com.google.firebase:firebase-crashlytics:18.2.10")

    // testing
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.3")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.4.0")

    // Hilt (Dependency Injection)
    // TODO: Hilt
//    implementation("com.google.dagger:hilt-android:2.38.1")
//    kapt("com.google.dagger:hilt-compiler:2.38.1")
}