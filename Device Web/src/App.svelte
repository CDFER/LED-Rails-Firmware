<script lang="ts">
  import { onMount } from "svelte";
  import {
    Clock3,
    Map,
    Minus,
    Plus,
    Power,
    RefreshCw,
    RotateCcw,
    Save,
    Trash2,
    Wifi,
    WifiOff,
  } from "@lucide/svelte";
  import faviconUrl from "../favicon.png";

  type ModeOption = {
    id: number;
    name: string;
  };

  type BrightnessCurvePoint = {
    lux: number;
    brightness: number;
  };

  type DailyScheduleEntry = {
    enabled: boolean;
    minute: number;
    on: boolean;
    mode: number;
  };

  type DeviceStatus = {
    device: {
      city: string;
      firmware: string;
      version: string;
      board: string;
      uptime: number;
    };
    wifi: {
      connected: boolean;
      ssid?: string;
      rssi?: number;
      savedNetworks: string[];
    };
    leds: {
      on: boolean;
      brightness: number;
      minimumBrightness: number;
      maximumBrightness: number;
      automaticBrightness: boolean;
      ambientLux?: number;
      ambientBrightness?: number;
      curve?: BrightnessCurvePoint[];
    };
    mode: {
      current: number;
      name: string;
      available: ModeOption[];
    };
    schedule: DailyScheduleEntry[];
  };

  let status: DeviceStatus | null = null;
  let loading = true;
  let refreshing = false;
  let errorMessage = "";
  let notice = "";
  let busyAction = "";
  let curveDraft: BrightnessCurvePoint[] = [];
  let curveEditing = false;
  let draggingCurvePoint: number | null = null;
  let scheduleDraft: DailyScheduleEntry[] = [];
  let scheduleEditing = false;

  function cityName(city: string): string {
    const names: Record<string, string> = {
      akl: "Auckland",
      wlg: "Wellington",
      mel: "Melbourne",
    };
    return names[city.toLowerCase()] ?? city.toUpperCase();
  }

  function cityTheme(city: string): string {
    const themes: Record<string, string> = {
      akl: "city-auckland",
      wlg: "city-wellington",
      mel: "city-melbourne",
    };
    return themes[city.toLowerCase()] ?? "city-default";
  }

  function faviconTheme(city: string): { background: string; accent: string } {
    const themes: Record<string, { background: string; accent: string }> = {
      akl: { background: "#001231", accent: "#0073bd" },
      wlg: { background: "#001d34", accent: "#d9ed4c" },
      mel: { background: "#0073cf", accent: "#ffffff" },
    };
    return (
      themes[city.toLowerCase()] ?? {
        background: "#10191c",
        accent: "#83d8c3",
      }
    );
  }

  function faviconDataUrl(city: string | undefined): string {
    const { background, accent } = faviconTheme(city ?? "");
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="10" fill="${background}"/><path d="M16 18h32M16 32h32M16 46h32M22 12v40M42 12v40" fill="none" stroke="${accent}" stroke-width="6" stroke-linecap="round"/></svg>`;
    return `data:image/svg+xml,${encodeURIComponent(svg)}`;
  }

  function formatUptime(milliseconds: number): string {
    const totalSeconds = Math.floor(milliseconds / 1000);
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    if (days > 0) return `${days}d ${hours}h`;
    if (hours > 0) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  }

  function brightnessPercent(): number {
    // if the device is in automatic brightness mode, return the calculated brightness based on the curve and ambient lux
    if (status?.leds.automaticBrightness) {
      return Math.round(liveCurveBrightness());
    } else {
      return status?.leds.brightness ?? 0;
    }
  }

  function signalStrength(rssi: number | undefined): string {
    if (rssi === undefined) return "Unknown signal strength";
    if (rssi >= -50) return "Great signal strength";
    if (rssi >= -60) return "Good signal strength";
    if (rssi >= -70) return "Fair signal strength";
    return "Weak signal strength";
  }

  function deviceAddress(): string {
    return typeof window !== "undefined" ? window.location.hostname : "";
  }

  function copyCurve(
    curve: BrightnessCurvePoint[] | undefined,
  ): BrightnessCurvePoint[] {
    return (
      curve?.map((point) => ({
        lux: point.lux,
        brightness: point.brightness,
      })) ?? []
    );
  }

  function copySchedule(
    schedule: DailyScheduleEntry[] | undefined,
  ): DailyScheduleEntry[] {
    return schedule?.map((entry) => ({ ...entry })) ?? [];
  }

  function scheduleMapTimezone(): string {
    const city = status?.device.city.toLowerCase();
    return city === "mel"
      ? "Australia/Melbourne"
      : city === "akl" || city === "wlg"
        ? "Pacific/Auckland"
        : "UTC";
  }

  function timezoneOffsetMinutes(timeZone: string): number {
    const now = new Date();
    now.setSeconds(0, 0);
    const parts = new Intl.DateTimeFormat("en-US", {
      timeZone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      hourCycle: "h23",
    }).formatToParts(now);
    const values = Object.fromEntries(
      parts.map((part) => [part.type, part.value]),
    );
    return (
      (Date.UTC(
        Number(values.year),
        Number(values.month) - 1,
        Number(values.day),
        Number(values.hour),
        Number(values.minute),
      ) -
        now.getTime()) /
      60000
    );
  }

  function convertScheduleMinute(
    minute: number,
    fromTimeZone: string,
    toTimeZone: string,
  ): number {
    const convertedMinute =
      minute +
      timezoneOffsetMinutes(toTimeZone) -
      timezoneOffsetMinutes(fromTimeZone);
    return ((convertedMinute % 1440) + 1440) % 1440;
  }

  function formatScheduleTime(minute: number): string {
    const userMinute = convertScheduleMinute(
      minute,
      scheduleMapTimezone(),
      Intl.DateTimeFormat().resolvedOptions().timeZone,
    );
    const hours = Math.floor(userMinute / 60)
      .toString()
      .padStart(2, "0");
    const minutes = (userMinute % 60).toString().padStart(2, "0");
    return `${hours}:${minutes}`;
  }

  function scheduleTimezoneName(): string {
    return (
      Intl.DateTimeFormat(undefined, { timeZoneName: "short" })
        .formatToParts(new Date())
        .find((part) => part.type === "timeZoneName")?.value ?? "local time"
    );
  }

  function enabledScheduleCount(): number {
    return scheduleDraft.filter((entry) => entry.enabled).length;
  }

  function sortScheduleDraft(): void {
    scheduleDraft = [...scheduleDraft].sort((left, right) => {
      if (left.enabled !== right.enabled) return left.enabled ? -1 : 1;
      return left.minute - right.minute;
    });
  }

  function scheduleHasChanges(): boolean {
    return (
      JSON.stringify(scheduleDraft) !== JSON.stringify(status?.schedule ?? [])
    );
  }

  function updateScheduleEntry(
    index: number,
    field: keyof DailyScheduleEntry,
    value: string | boolean,
  ): void {
    scheduleDraft = scheduleDraft.map((entry, entryIndex) => {
      if (entryIndex !== index) return entry;
      if (field === "enabled" || field === "on")
        return { ...entry, [field]: value === true || value === "1" };
      return { ...entry, [field]: Number(value) };
    });
    sortScheduleDraft();
    scheduleEditing = true;
  }

  function updateScheduleTime(index: number, value: string): void {
    const [hours, minutes] = value.split(":").map(Number);
    if (!Number.isFinite(hours) || !Number.isFinite(minutes)) return;
    const userMinute = hours * 60 + minutes;
    const mapMinute = convertScheduleMinute(
      userMinute,
      Intl.DateTimeFormat().resolvedOptions().timeZone,
      scheduleMapTimezone(),
    );
    updateScheduleEntry(index, "minute", String(mapMinute));
  }

  function addScheduleAction(): void {
    const availableIndex = scheduleDraft.findIndex((entry) => !entry.enabled);
    if (availableIndex < 0) return;
    const activeEntries = scheduleDraft.filter((entry) => entry.enabled);
    const lastMinute = activeEntries.at(-1)?.minute ?? 0;
    if (lastMinute >= 1439) return;
    activeEntries.push({
      enabled: true,
      minute: Math.min(lastMinute + 60, 1439),
      on: true,
      mode: status?.mode.current ?? 0,
    });
    scheduleDraft = [
      ...activeEntries,
      ...scheduleDraft.filter((entry) => !entry.enabled).slice(1),
    ];
    scheduleEditing = true;
  }

  function removeScheduleAction(index: number): void {
    scheduleDraft = [
      ...scheduleDraft.filter(
        (entry, entryIndex) => entry.enabled && entryIndex !== index,
      ),
      ...scheduleDraft.filter((entry) => !entry.enabled),
      { ...scheduleDraft[index], enabled: false },
    ].slice(0, 8);
    scheduleEditing = true;
  }

  function isCurveValid(): boolean {
    if (curveDraft.length !== 4 || curveDraft[0].lux !== 0) return false;
    return curveDraft.every((point, index) => {
      const previousPoint = curveDraft[index - 1];
      return (
        Number.isFinite(point.lux) &&
        Number.isFinite(point.brightness) &&
        point.lux >= 0 &&
        point.lux < 1_000_000 &&
        point.brightness >= 0 &&
        point.brightness <= 100 &&
        (index === 0 ||
          (point.lux > previousPoint.lux &&
            point.brightness >= previousPoint.brightness))
      );
    });
  }

  function curveHasChanges(): boolean {
    if (!status?.leds.curve || status.leds.curve.length !== curveDraft.length)
      return false;
    return curveDraft.some(
      (point, index) =>
        point.lux !== status?.leds.curve?.[index].lux ||
        point.brightness !== status?.leds.curve?.[index].brightness,
    );
  }

  function updateCurvePoint(
    index: number,
    field: keyof BrightnessCurvePoint,
    value: string,
  ): void {
    const numericValue = Number(value);
    curveDraft = curveDraft.map((point, pointIndex) =>
      pointIndex === index ? { ...point, [field]: numericValue } : point,
    );
    curveEditing = true;
  }

  function resetCurveDraft(): void {
    curveDraft = copyCurve(status?.leds.curve);
    curveEditing = false;
  }

  function resetScheduleDraft(): void {
    scheduleDraft = copySchedule(status?.schedule);
    scheduleEditing = false;
  }

  function curveBarWidth(brightness: number): number {
    return Math.max(0, Math.min(100, brightness));
  }

  function curveLuxMax(): number {
    return Math.max(curveDraft[curveDraft.length - 1]?.lux ?? 1, 1);
  }

  function curvePointX(lux: number): number {
    if (lux <= 0) return 56;
    const boundedLux = Math.min(curveLuxMax(), lux);
    return 56 + (Math.log10(boundedLux) / Math.log10(curveLuxMax())) * 560;
  }

  function curvePointY(brightness: number): number {
    return 224 - (curveBarWidth(brightness) / 100) * 184;
  }

  function liveCurvePointX(): number {
    return curvePointX(status?.leds.ambientLux ?? 0);
  }

  function liveCurveBrightness(): number {
    const ambientLux = status?.leds.ambientLux ?? 0;
    if (curveDraft.length === 0 || ambientLux <= curveDraft[0].lux)
      return curveDraft[0]?.brightness ?? 0;

    for (let index = 1; index < curveDraft.length; index++) {
      const previousPoint = curveDraft[index - 1];
      const currentPoint = curveDraft[index];
      if (ambientLux <= currentPoint.lux) {
        const previousX = curvePointX(previousPoint.lux);
        const currentX = curvePointX(currentPoint.lux);
        const ratio =
          currentX === previousX
            ? 0
            : (liveCurvePointX() - previousX) / (currentX - previousX);
        return (
          previousPoint.brightness +
          (currentPoint.brightness - previousPoint.brightness) * ratio
        );
      }
    }

    return curveDraft[curveDraft.length - 1].brightness;
  }

  function liveCurvePointY(): number {
    return curvePointY(liveCurveBrightness());
  }

  function startCurveDrag(index: number, event: PointerEvent): void {
    draggingCurvePoint = index;
    (event.currentTarget as SVGCircleElement).setPointerCapture(
      event.pointerId,
    );
  }

  function dragCurvePoint(event: PointerEvent): void {
    if (draggingCurvePoint === null) return;
    const graph = event.currentTarget as SVGSVGElement;
    const graphBounds = graph.getBoundingClientRect();
    const graphY =
      ((event.clientY - graphBounds.top) / graphBounds.height) * 280;
    let brightness = Math.round(((224 - graphY) / 184) * 1000) / 10;
    const previousPoint = curveDraft[draggingCurvePoint - 1];
    const nextPoint = curveDraft[draggingCurvePoint + 1];
    brightness = Math.max(
      previousPoint?.brightness ?? 0,
      Math.min(nextPoint?.brightness ?? 100, brightness),
    );
    curveDraft = curveDraft.map((point, index) =>
      index === draggingCurvePoint
        ? { ...point, brightness: Math.max(0, Math.min(100, brightness)) }
        : point,
    );
    curveEditing = true;
  }

  function stopCurveDrag(): void {
    draggingCurvePoint = null;
  }

  function adjustCurvePoint(index: number, event: KeyboardEvent): void {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    const direction = event.key === "ArrowUp" ? 1 : -1;
    const point = curveDraft[index];
    if (!point) return;
    const previousPoint = curveDraft[index - 1];
    const nextPoint = curveDraft[index + 1];
    const brightness = Math.max(
      previousPoint?.brightness ?? 0,
      Math.min(nextPoint?.brightness ?? 100, point.brightness + direction),
    );
    curveDraft = curveDraft.map((currentPoint, pointIndex) =>
      pointIndex === index ? { ...currentPoint, brightness } : currentPoint,
    );
    curveEditing = true;
  }

  async function refreshStatus(showProgress = false): Promise<void> {
    if (showProgress) refreshing = true;
    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok)
        throw new Error(`Status request failed (${response.status})`);
      status = (await response.json()) as DeviceStatus;
      if (!curveEditing) curveDraft = copyCurve(status.leds.curve);
      if (!scheduleEditing) scheduleDraft = copySchedule(status.schedule);
      errorMessage = "";
    } catch (error) {
      errorMessage =
        error instanceof Error ? error.message : "Could not reach the device.";
    } finally {
      loading = false;
      refreshing = false;
    }
  }

  async function applySetting(
    action: string,
    path: string,
    values: Record<string, string>,
    successMessage: string,
  ): Promise<boolean> {
    busyAction = action;
    errorMessage = "";
    notice = "";
    try {
      const response = await fetch(path, {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
        },
        body: new URLSearchParams(values),
      });
      if (!response.ok) {
        const payload = (await response.json().catch(() => null)) as {
          error?: string;
        } | null;
        throw new Error(
          payload?.error ?? `Request failed (${response.status})`,
        );
      }
      notice = successMessage;
      window.setTimeout(() => void refreshStatus(), 320);
      return true;
    } catch (error) {
      errorMessage =
        error instanceof Error
          ? error.message
          : "The setting could not be saved.";
      return false;
    } finally {
      busyAction = "";
    }
  }

  async function setPower(enabled: boolean): Promise<void> {
    await applySetting(
      "power",
      "/api/led/power",
      { on: enabled ? "1" : "0" },
      enabled ? "LED output enabled." : "LED output disabled.",
    );
  }

  async function changeBrightness(direction: "up" | "down"): Promise<void> {
    await applySetting(
      `brightness-${direction}`,
      "/api/led/brightness",
      { direction },
      direction === "up" ? "Brightness increased." : "Brightness decreased.",
    );
  }

  async function saveBrightnessCurve(): Promise<void> {
    if (!isCurveValid()) {
      errorMessage = "Curve points must increase in lux and brightness.";
      return;
    }

    const values: Record<string, string> = {};
    curveDraft.forEach((point, index) => {
      values[`lux${index}`] = String(point.lux);
      values[`brightness${index}`] = String(point.brightness);
    });

    const saved = await applySetting(
      "curve",
      "/api/led/curve",
      values,
      "Brightness curve saved.",
    );
    if (saved) curveEditing = false;
  }

  async function saveDailySchedule(): Promise<void> {
    sortScheduleDraft();
    const values: Record<string, string> = {};
    scheduleDraft.forEach((entry, index) => {
      values[`entry${index}Enabled`] = entry.enabled ? "1" : "0";
      values[`entry${index}Minute`] = String(entry.minute);
      values[`entry${index}On`] = entry.on ? "1" : "0";
      values[`entry${index}Mode`] = String(entry.mode);
    });
    const saved = await applySetting(
      "schedule",
      "/api/schedule",
      values,
      "Schedule saved.",
    );
    if (saved) scheduleEditing = false;
  }

  async function selectMode(mode: ModeOption): Promise<void> {
    if (status?.mode.current === mode.id) return;
    await applySetting(
      "mode",
      "/api/mode",
      { mode: String(mode.id) },
      `${mode.name} mode selected.`,
    );
  }

  async function forgetWifi(networkName: string): Promise<void> {
    if (!window.confirm(`Forget ${networkName}?`)) return;
    await applySetting(
      "wifi-forget",
      "/api/wifi/forget",
      { ssid: networkName },
      `${networkName} removed.`,
    );
  }

  onMount(() => {
    void refreshStatus();
    const poll = window.setInterval(() => void refreshStatus(), 5000);
    return () => window.clearInterval(poll);
  });
</script>

<svelte:head>
  <link
    rel="icon"
    type="image/svg+xml"
    href={faviconDataUrl(status?.device.city)}
  />
  <link rel="icon" type="image/png" href={faviconUrl} />
  <title
    >{status
      ? `${cityName(status.device.city)} Train Map | Controls`
      : "Train Map | Controls"}</title
  >
</svelte:head>

<div
  class={`app-shell ${status ? cityTheme(status.device.city) : "city-default"}`}
>
  <header class="topbar">
    <div class="brand">
      <div class="brand-mark" aria-hidden="true">
        <Map size={21} strokeWidth={2.25} />
      </div>
      <div>
        <p class="eyebrow">Kea Studios</p>
        <h1>Control Panel</h1>
      </div>
    </div>
    <div class:online={status?.wifi.connected} class="connection-state">
      {#if status?.wifi.connected}
        <Wifi size={17} strokeWidth={2.2} />
        <span>Online</span>
      {:else}
        <WifiOff size={17} strokeWidth={2.2} />
        <span>Offline</span>
      {/if}
    </div>
  </header>

  <main>
    {#if loading && !status}
      <section class="loading-state" aria-live="polite">
        <div class="loading-track"><span></span><span></span><span></span></div>
        <p>Connecting to device</p>
      </section>
    {:else if !status}
      <section class="error-state" aria-live="assertive">
        <h2>Device status is unavailable</h2>
        <p>
          {errorMessage || "Check that this page is opened from the train map."}
        </p>
        <button
          class="command-button"
          type="button"
          onclick={() => void refreshStatus(true)}
        >
          <RefreshCw size={17} /> Try again
        </button>
      </section>
    {:else}
      <section class="device-banner" aria-labelledby="device-title">
        <div>
          <h2 id="device-title">{cityName(status.device.city)} Train Map</h2>
          <p class="device-detail">{status.device.board}</p>
        </div>
        <button
          class="icon-button refresh-button"
          class:spinning={refreshing}
          type="button"
          onclick={() => void refreshStatus(true)}
          disabled={refreshing || busyAction !== ""}
          aria-label="Refresh device status"
          title="Refresh device status"
        >
          <RefreshCw size={18} />
        </button>
      </section>

      <p
        class:error-notice={errorMessage}
        class:notice-visible={errorMessage || notice}
        class="notice"
        aria-live="polite"
      >
        {errorMessage || notice}
      </p>

      <div class="control-grid">
        <section class="panel output-panel" aria-labelledby="output-title">
          <div class="panel-heading">
            <div>
              <p class="eyebrow">Current</p>
              <h3 id="output-title">Brightness</h3>
            </div>
            <label class="toggle" title="Turn LED output on or off">
              <input
                type="checkbox"
                checked={status.leds.on}
                onchange={(event) => void setPower(event.currentTarget.checked)}
                disabled={busyAction !== ""}
                aria-label="Toggle LED output"
              />
              <span class="toggle-track"
                ><span class="toggle-thumb"><Power size={13} /></span></span
              >
            </label>
          </div>
          <div class="brightness-readout">
            <strong>{brightnessPercent()}<small>%</small></strong>
          </div>
          <div class="brightness-bar" aria-hidden="true">
            <span style={`width: ${brightnessPercent()}%`}></span>
          </div>
          {#if !status.leds.automaticBrightness}
            <div class="brightness-controls">
              <button
                class="icon-button"
                type="button"
                onclick={() => void changeBrightness("down")}
                disabled={busyAction !== ""}
                aria-label="Decrease brightness"
                title="Decrease brightness"
              >
                <Minus size={19} />
              </button>
              <p>Manual brightness control</p>
              <button
                class="icon-button"
                type="button"
                onclick={() => void changeBrightness("up")}
                disabled={busyAction !== ""}
                aria-label="Increase brightness"
                title="Increase brightness"
              >
                <Plus size={19} />
              </button>
            </div>
          {/if}
        </section>

        <section class="panel mode-panel" aria-labelledby="mode-title">
          <div class="panel-heading">
            <div>
              <p class="eyebrow">Firmware</p>
              <h3 id="mode-title">Modes</h3>
            </div>
          </div>
          <div class="mode-list" aria-label="Display mode">
            {#each status.mode.available as mode}
              <button
                class:active={status.mode.current === mode.id}
                class="mode-button"
                type="button"
                onclick={() => void selectMode(mode)}
                disabled={busyAction !== ""}
              >
                {mode.name}
              </button>
            {/each}
          </div>
        </section>

        <section class="panel schedule-panel" aria-labelledby="schedule-title">
          <div class="panel-heading schedule-heading">
            <div>
              <p class="eyebrow">Every day</p>
              <h3 id="schedule-title">Schedule</h3>
              <p class="panel-footnote">
                Set when your map automatically turns on or off. Times are in {scheduleTimezoneName()}.
              </p>
            </div>
            <div class="curve-actions">
              <button
                class="command-button add-schedule-button"
                type="button"
                onclick={addScheduleAction}
                disabled={busyAction !== "" || enabledScheduleCount() >= 8}
              >
                <Plus size={16} /> Add time
              </button>
              <button
                class="icon-button"
                type="button"
                onclick={resetScheduleDraft}
                disabled={busyAction !== "" || !scheduleHasChanges()}
                aria-label="Reset daily schedule"
                title="Reset daily schedule"
              >
                <RotateCcw size={17} />
              </button>
              <button
                class="command-button curve-save-button"
                type="button"
                onclick={() => void saveDailySchedule()}
                disabled={busyAction !== "" || !scheduleHasChanges()}
              >
                <Save size={16} /> Save schedule
              </button>
            </div>
          </div>
          <div class="schedule-list">
            {#each scheduleDraft as entry, index}
              {#if entry.enabled}
                <div class="schedule-row">
                  <button
                    class="icon-button danger-button schedule-remove"
                    type="button"
                    onclick={() => removeScheduleAction(index)}
                    disabled={busyAction !== ""}
                    aria-label={`Remove scheduled time at ${formatScheduleTime(entry.minute)}`}
                    title="Remove scheduled time"
                  >
                    <Trash2 size={15} />
                  </button>
                  <Clock3 size={18} class="schedule-icon" aria-hidden="true" />
                  <input
                    class="time-input"
                    type="time"
                    value={formatScheduleTime(entry.minute)}
                    onchange={(event) =>
                      updateScheduleTime(index, event.currentTarget.value)}
                    disabled={busyAction !== ""}
                    aria-label={`Scheduled action time ${index + 1}`}
                  />
                  <select
                    class="schedule-select"
                    value={entry.on ? "on" : "off"}
                    onchange={(event) =>
                      updateScheduleEntry(
                        index,
                        "on",
                        event.currentTarget.value === "on",
                      )}
                    disabled={busyAction !== ""}
                    aria-label={`Action at ${formatScheduleTime(entry.minute)}`}
                  >
                    <option value="on">Turn on</option>
                    <option value="off">Turn off</option>
                  </select>
                  {#if entry.on}
                    <select
                      class="schedule-select schedule-mode"
                      value={entry.mode}
                      onchange={(event) =>
                        updateScheduleEntry(
                          index,
                          "mode",
                          event.currentTarget.value,
                        )}
                      disabled={busyAction !== ""}
                      aria-label={`Mode at ${formatScheduleTime(entry.minute)}`}
                    >
                      {#each status.mode.available as mode}
                        <option value={mode.id}>{mode.name} Mode</option>
                      {/each}
                    </select>
                  {/if}
                </div>
              {/if}
            {/each}
            {#if enabledScheduleCount() === 0}
              <p class="empty-list schedule-empty">No scheduled times</p>
            {/if}
          </div>
        </section>

        {#if status.leds.automaticBrightness && status.leds.curve}
          <section class="panel curve-panel" aria-labelledby="curve-title">
            <div class="panel-heading curve-heading">
              <div>
                <p class="eyebrow">Ambient light sensor</p>
                <h3 id="curve-title">Brightness Curve Editor</h3>
              </div>
              <div class="curve-actions">
                <button
                  class="icon-button"
                  type="button"
                  onclick={resetCurveDraft}
                  disabled={busyAction !== "" || !curveHasChanges()}
                  aria-label="Reset brightness curve"
                  title="Reset brightness curve"
                >
                  <RotateCcw size={17} />
                </button>
                <button
                  class="command-button curve-save-button"
                  type="button"
                  onclick={() => void saveBrightnessCurve()}
                  disabled={busyAction !== "" ||
                    !curveHasChanges() ||
                    !isCurveValid()}
                >
                  <Save size={16} /> Save curve
                </button>
              </div>
            </div>
            <div class="curve-graph-wrap">
              <svg
                class="curve-graph"
                viewBox="0 0 640 280"
                role="img"
                aria-label="Brightness by ambient light level"
                onpointermove={dragCurvePoint}
                onpointerup={stopCurveDrag}
                onpointercancel={stopCurveDrag}
              >
                <text class="graph-axis-title graph-y-title" x="14" y="24"
                  >Brightness %</text
                >
                <text class="graph-axis-title" x="330" y="270"
                  >Ambient light (lux)</text
                >
                {#each [0, 25, 50, 75, 100] as tick}
                  <line
                    class="graph-grid-line"
                    x1="56"
                    x2="616"
                    y1={curvePointY(tick)}
                    y2={curvePointY(tick)}
                  />
                  <text
                    class="graph-tick graph-y-tick"
                    x="46"
                    y={curvePointY(tick) + 4}>{tick}</text
                  >
                {/each}
                <line class="graph-axis" x1="56" x2="616" y1="224" y2="224" />
                <line class="graph-axis" x1="56" x2="56" y1="40" y2="224" />
                {#each curveDraft as point, index}
                  <line
                    class="graph-tick-line"
                    x1={curvePointX(point.lux)}
                    x2={curvePointX(point.lux)}
                    y1="224"
                    y2="230"
                  />
                  <text
                    class="graph-tick graph-x-tick"
                    class:graph-context-start={index === 0}
                    class:graph-context-middle={index > 0 &&
                      index < curveDraft.length - 1}
                    class:graph-context-end={index === curveDraft.length - 1}
                    x={curvePointX(point.lux)}
                    y="244"
                  >
                    {point.lux}
                  </text>
                {/each}
                <text
                  class="graph-context-label graph-context-start"
                  x="56"
                  y="260">Pitch Black</text
                >
                <text
                  class="graph-context-label graph-context-end"
                  x="616"
                  y="260">Daylight</text
                >
                {#if curveDraft.length > 1}
                  <polyline
                    class="curve-line"
                    points={curveDraft
                      .map(
                        (point) =>
                          `${curvePointX(point.lux)},${curvePointY(point.brightness)}`,
                      )
                      .join(" ")}
                  />
                {/if}
                {#if status.leds.ambientLux !== undefined && status.leds.ambientBrightness !== undefined}
                  <circle
                    class="live-curve-point"
                    cx={liveCurvePointX()}
                    cy={liveCurvePointY()}
                    r="8"
                    aria-label={`Live brightness at ${status.leds.ambientLux} lux`}
                  />
                {/if}
                {#each curveDraft as point, index}
                  <circle
                    class:dragging={draggingCurvePoint === index}
                    class="curve-point"
                    cx={curvePointX(point.lux)}
                    cy={curvePointY(point.brightness)}
                    r="7"
                    role="slider"
                    aria-label={`Brightness at ${point.lux} lux`}
                    aria-valuemin="0"
                    aria-valuemax="100"
                    aria-valuenow={point.brightness}
                    tabindex="0"
                    onpointerdown={(event) => startCurveDrag(index, event)}
                    onkeydown={(event) => adjustCurvePoint(index, event)}
                  />
                {/each}
              </svg>
            </div>
          </section>
        {/if}

        <section class="panel network-panel" aria-labelledby="network-title">
          <div class="panel-heading">
            <div>
              <p class="eyebrow">Network</p>
              <h3 id="network-title">Wi-Fi</h3>
            </div>
            {#if status.wifi.connected && status.wifi.rssi !== undefined}
              <div class="signal-reading">
                <strong>{status.wifi.rssi} dBm</strong>
                <span>{signalStrength(status.wifi.rssi)}</span>
              </div>
            {/if}
          </div>
          <div class="network-summary">
            {#if status.wifi.connected}
              <Wifi size={21} strokeWidth={2} />
              <div>
                <strong>{status.wifi.ssid}</strong>
                <a
                  class="device-address"
                  href={`http://${deviceAddress()}`}
                  target="_blank"
                  rel="noreferrer">http://{deviceAddress()}</a
                >
              </div>
            {:else}
              <WifiOff size={21} strokeWidth={2} />
              <div>
                <strong>Not connected</strong><span
                  >Saved networks will be retried automatically.</span
                >
              </div>
            {/if}
          </div>

          <div class="saved-networks">
            {#if status.wifi.savedNetworks.length > 0}
              <ul>
                {#each status.wifi.savedNetworks as networkName}
                  <li>
                    <span>{networkName}</span>
                    <button
                      class="icon-button danger-button"
                      type="button"
                      onclick={() => void forgetWifi(networkName)}
                      disabled={busyAction !== ""}
                      aria-label={`Forget ${networkName}`}
                      title={`Forget ${networkName}`}
                    >
                      <Trash2 size={16} />
                    </button>
                  </li>
                {/each}
              </ul>
            {:else}
              <p class="empty-list">No saved networks</p>
            {/if}
          </div>
        </section>
      </div>

      <footer class="device-footer">
        <span>Uptime {formatUptime(status.device.uptime)}</span>
        <span>{status.device.firmware} {status.device.version}</span>
      </footer>
    {/if}
  </main>
</div>
