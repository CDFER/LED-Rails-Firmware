<script lang="ts">
  import { onMount } from "svelte";
  import { initDialogCustomizations } from "./ewt-customizations";

  type VersionId = "AKL_V1_0_0" | "AKL_V1_1_0" | "WLG_V1_0_0" | "MEL_V1_0_0";

  type VersionOption = {
    id: VersionId;
    label: string;
  };

  const versions: VersionOption[] = [
    { id: "AKL_V1_0_0", label: "Auckland V1.0" },
    { id: "AKL_V1_1_0", label: "Auckland V1.1" },
    { id: "WLG_V1_0_0", label: "Wellington V1.0" },
    { id: "MEL_V1_0_0", label: "Melbourne V1.0" },
  ];

  let selectedVersion: VersionId = "AKL_V1_1_0";
  let manifestPath = getManifestPath(selectedVersion);
  let installer: HTMLElement | undefined;
  let supported = true;

  function getManifestPath(version: VersionId): string {
    return `bin/${version}/manifest.json`;
  }

  function selectVersion(version: VersionId): void {
    selectedVersion = version;
    manifestPath = getManifestPath(version);
  }

  onMount(() => {
    if (!installer) {
      return;
    }

    const updateSupportState = (): void => {
      supported = !installer?.hasAttribute("install-unsupported");
    };

    updateSupportState();
    const supportObserver = new MutationObserver(updateSupportState);
    supportObserver.observe(installer, {
      attributes: true,
      attributeFilter: ["install-unsupported"],
    });

    const stopDialogCustomizations = initDialogCustomizations();

    return () => {
      supportObserver.disconnect();
      stopDialogCustomizations();
    };
  });
</script>

<svelte:head>
  <title>Kea Studios Setup</title>
</svelte:head>

<div class="content">
  <header>
    <p class="eyebrow">Kea Studios / LED Rails</p>
    <h1>Setup your train map</h1>
    <p>Wi-Fi setup and firmware updates for your Live Train Map.</p>
  </header>

  <main>
    <div class="installer-panel">
      {#if supported}
        <ol class="instructions">
          <li>
            <strong>Connect your map</strong>
            <span
              >Plug your map into your computer with the supplied USB cable.</span
            >
            <strong
              >Use the left USB port on Auckland and Melbourne maps.</strong
            >
            <small>Once setup you can power it from any of the USB ports.</small
            >
          </li>
          <li>
            <strong>Choose your map</strong>
            <span
              >Select the city and version printed on the back of your map.</span
            >
          </li>
          <li>
            <strong>Connect to the map</strong>
            <span
              >Click Connect and select the correct COM port. Look for the port
              labelled:</span
            >
            <strong>USB JTAG/serial debug unit</strong>
          </li>
          <li>
            <strong>Enter your Wi-Fi</strong>
            <span>Select Connect to Wi-Fi and follow the prompts.</span>
            <small
              >From the pop-up you can also install the latest firmware.</small
            >
          </li>
        </ol>

        <div class="controls">
          <div class="version-heading">
            <span class="section-label">Select your city and version</span>
            <span class="selected-version"
              >{versions.find((version) => version.id === selectedVersion)
                ?.label}</span
            >
          </div>
          <div class="version-btn-group" aria-label="Select hardware version">
            {#each versions as version}
              <button
                type="button"
                class:selected={selectedVersion === version.id}
                class="version-btn"
                onclick={() => selectVersion(version.id)}
              >
                {version.label}
              </button>
            {/each}
          </div>

          <esp-web-install-button
            bind:this={installer}
            id="inst"
            manifest={manifestPath}
            improv-wifi
          >
            <button class="connect-btn" type="button" slot="activate"
              >Connect</button
            >
            <span slot="unsupported"
              >Sorry, your browser does not support Web Serial. Please use
              Firefox, Chrome, or Edge.</span
            >
            <span slot="not-allowed"
              >This tool requires HTTPS. Please use a secure connection.</span
            >
          </esp-web-install-button>
        </div>
      {:else}
        <div class="unsupported-state">
          <span class="status-mark" aria-hidden="true">!</span>
          <h2>Web Serial is unavailable</h2>
          <p>Please open this installer in Firefox, Chrome, or Edge.</p>
        </div>
      {/if}
    </div>

    <p class="help-copy">
      Need help? Quick start guides are available for
      <a
        href="https://keastudios.co.nz/akl-ltm-quick-start/"
        target="_blank"
        rel="noreferrer">Auckland</a
      >,
      <a
        href="https://keastudios.co.nz/wlg-ltm-quick-start/"
        target="_blank"
        rel="noreferrer">Wellington</a
      >, and
      <a
        href="https://keastudios.co.nz/mel-ltm-quick-start/"
        target="_blank"
        rel="noreferrer">Melbourne</a
      >.
    </p>

    <section class="features" aria-labelledby="features-heading">
      <div class="section-intro">
        <span class="section-label">What's new</span>
        <h2 id="features-heading">Firmware Updates</h2>
      </div>
      <div class="feature-list">
        <article class="feature-item">
          <span class="feature-version">V1.4.0 August 2026</span>
          <h3>Smooth and configurable</h3>
          <p>
            Fade transistions, and fine grained control on the new device page.
            Press <strong>Visit Device</strong> from the pop-up. This page lets you
            set a time for you map to turn off and maunally control the ambinent
            light curve.
          </p>
        </article>
        <article class="feature-item">
          <span class="feature-version">V1.3.0</span>
          <h3>Melbourne maps are here</h3>
          <p>
            Realtime train tracking across the Metro network, with support for
            the ESP32-S3 chip.
          </p>
        </article>
        <article class="feature-item">
          <span class="feature-version">V1.2.2</span>
          <h3>Auckland has a fourth mode</h3>
          <p>
            Show realtime trains while hiding purple out-of-service trains. Long
            press the power button to cycle modes.
          </p>
        </article>
        <article class="feature-item">
          <span class="feature-version">V1.2.1</span>
          <h3>Offline timetable mode</h3>
          <p>
            Use the October 2025 weekday timetable when Wi-Fi is unavailable, or
            run it at 600x speed.
          </p>
        </article>
      </div>
    </section>
  </main>

  <footer>
    Powered by <a
      href="https://esphome.github.io/esp-web-tools/"
      target="_blank"
      rel="noreferrer">ESP Web Tools</a
    >
  </footer>
</div>
