type DialogRoot = Element | ShadowRoot;
type CustomDialog = HTMLElement & {
	__customInstallCopyInit?: boolean;
	__customNoPortCopyInit?: boolean;
	__eraseStepDetected?: boolean;
	__eraseStepSkipped?: boolean;
	__eraseStepClickAttempted?: boolean;
	__loggedEraseButtons?: boolean;
	__lastEraseWaitLog?: number;
	__lastNextClickAt?: number;
};

type ButtonDescriptor = {
	button: Element;
	tag: string;
	slot: string;
	action: string;
	ariaLabel: string;
	text: string;
	id: string;
	className: string;
	value: string;
	role: string;
	searchText: string;
	disabled: boolean;
};

function normalizeText(value: string | null | undefined): string {
	return (value ?? '').replace(/\s+/g, ' ').trim();
}

function collectRoots(host: DialogRoot): DialogRoot[] {
	const stack: DialogRoot[] = [host];
	const visited = new Set<DialogRoot>();
	const roots: DialogRoot[] = [];

	while (stack.length) {
		const current = stack.pop();
		if (!current || visited.has(current)) {
			continue;
		}

		visited.add(current);
		roots.push(current);

		current.querySelectorAll('*').forEach((element) => {
			const shadowRoot = (element as HTMLElement).shadowRoot;
			if (shadowRoot && !visited.has(shadowRoot)) {
				stack.push(shadowRoot);
			}
		});

		const shadowRoot = (current as HTMLElement).shadowRoot;
		if (shadowRoot && !visited.has(shadowRoot)) {
			stack.push(shadowRoot);
		}
	}

	return roots;
}

function observeUntilApplied(
	targetHost: CustomDialog,
	applyUpdate: () => boolean,
	timeoutMs: number,
	watchCharacterData: boolean,
): () => void {
	if (applyUpdate()) {
		return () => undefined;
	}

	let rafQueued = false;
	let timer: number | undefined;
	let timeoutHandle: number | undefined;
	let stopped = false;

	const observer = new MutationObserver(() => {
		if (rafQueued || stopped) {
			return;
		}

		rafQueued = true;
		requestAnimationFrame(() => {
			rafQueued = false;
			runApply();
		});
	});

	const runApply = (): void => {
		if (stopped) {
			return;
		}

		if (applyUpdate()) {
			stop();
		}
	};

	const stop = (): void => {
		if (stopped) {
			return;
		}

		stopped = true;
		observer.disconnect();
		if (timer !== undefined) {
			window.clearInterval(timer);
		}
		if (timeoutHandle !== undefined) {
			window.clearTimeout(timeoutHandle);
		}
	};

	const observerOptions: MutationObserverInit = {
		childList: true,
		subtree: true,
		characterData: watchCharacterData,
	};

	observer.observe(targetHost, observerOptions);
	if (targetHost.shadowRoot) {
		observer.observe(targetHost.shadowRoot, observerOptions);
	}

	timer = window.setInterval(runApply, 120);
	timeoutHandle = window.setTimeout(stop, timeoutMs);

	return stop;
}

function trySkipEraseStep(dialog: CustomDialog, roots: DialogRoot[]): boolean {
	const fullText = normalizeText(roots.map((root) => root.textContent ?? '').join(' '));
	const isEraseStep = /erase\s+device/i.test(fullText)
		&& /all\s+data\s+on\s+the\s+device\s+will\s+be\s+lost/i.test(fullText);

	if (!isEraseStep) {
		if (dialog.__eraseStepClickAttempted) {
			dialog.__eraseStepSkipped = true;
			return true;
		}

		dialog.__eraseStepDetected = false;
		return false;
	}

	if (!dialog.__eraseStepDetected) {
		dialog.__eraseStepDetected = true;
	}

	roots.forEach((root) => {
		root.querySelectorAll('input[type="checkbox"]').forEach((element) => {
			const checkbox = element as HTMLInputElement;
			if (checkbox.checked) {
				checkbox.checked = false;
				checkbox.dispatchEvent(new Event('input', { bubbles: true }));
				checkbox.dispatchEvent(new Event('change', { bubbles: true }));
			}
		});
	});

	const buttonCandidates: Element[] = [];
	const buttonSelector = [
		'button',
		'md-filled-button',
		'md-outlined-button',
		'md-text-button',
		'md-tonal-button',
		'md-elevated-button',
		'mwc-button',
		'ewt-button',
		'[role="button"]',
		'[slot="primaryAction"]',
		'[slot="secondaryAction"]',
		'[dialog-action]',
		'[dialogaction]',
		'[data-action]',
		'[action]',
	].join(',');

	roots.forEach((root) => {
		root.querySelectorAll(buttonSelector).forEach((element) => {
			if (!buttonCandidates.includes(element)) {
				buttonCandidates.push(element);
			}
		});
	});

	const getButtonDescriptor = (button: Element): ButtonDescriptor => {
		const element = button as HTMLElement & { disabled?: boolean };
		const tag = button.tagName.toLowerCase();
		const slot = normalizeText(button.getAttribute('slot')).toLowerCase();
		const action = normalizeText(
			button.getAttribute('dialog-action')
			|| button.getAttribute('dialogaction')
			|| button.getAttribute('data-action')
			|| button.getAttribute('action'),
		).toLowerCase();
		const ariaLabel = normalizeText(button.getAttribute('aria-label'));
		const text = normalizeText(element.innerText || button.textContent);
		const id = button.id.toLowerCase();
		const className = normalizeText(button.getAttribute('class')).toLowerCase();
		const value = normalizeText(button.getAttribute('value')).toLowerCase();
		const role = normalizeText(button.getAttribute('role')).toLowerCase();
		const disabled = Boolean(
			element.disabled
			|| button.hasAttribute('disabled')
			|| button.getAttribute('aria-disabled') === 'true',
		);
		const searchText = normalizeText([ariaLabel, text, slot, action, id, className, value, role, tag].join(' ')).toLowerCase();

		return { button, tag, slot, action, ariaLabel, text, id, className, value, role, searchText, disabled };
	};

	const buttonDescriptors = buttonCandidates.map(getButtonDescriptor);
	let continueButtonInfo = buttonDescriptors.find((info) => !info.disabled && /\bnext\b/.test(info.searchText));

	if (!continueButtonInfo) {
		continueButtonInfo = buttonDescriptors.find((info) => !info.disabled && /\bnext\b/.test(info.action));
	}

	if (!continueButtonInfo) {
		continueButtonInfo = buttonDescriptors.find((info) => !info.disabled && info.slot === 'primaryaction');
	}

	if (!continueButtonInfo) {
		continueButtonInfo = buttonDescriptors.find((info) => {
			if (info.disabled || /erase|wipe/.test(info.searchText) || /back|cancel|close/.test(info.searchText)) {
				return false;
			}
			return /install|continue|confirm|proceed/.test(info.searchText);
		});
	}

	if (!continueButtonInfo && buttonDescriptors.length === 2) {
		continueButtonInfo = buttonDescriptors.find((info) => !info.disabled && info.slot !== 'secondaryaction');
	}

	if (!continueButtonInfo) {
		return false;
	}

	const continueButton = continueButtonInfo.button;
	const clickTarget = (continueButton as HTMLElement).shadowRoot?.querySelector('button, [role="button"]')
		|| continueButton;
	const now = Date.now();

	if (dialog.__lastNextClickAt && now - dialog.__lastNextClickAt < 1000) {
		return false;
	}

	dialog.__lastNextClickAt = now;
	dialog.__eraseStepClickAttempted = true;
	(clickTarget as HTMLElement).click();
	clickTarget.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, composed: true }));

	return false;
}

function customizeInstallDialog(dialog: CustomDialog): () => void {
	if (dialog.__customInstallCopyInit) {
		return () => undefined;
	}

	dialog.__customInstallCopyInit = true;
	dialog.__eraseStepSkipped = false;

	const applyCustomCopy = (): boolean => {
		if (dialog.__eraseStepSkipped || !document.body.contains(dialog)) {
			return true;
		}

		const roots = collectRoots(dialog);
		roots.forEach((root) => {
			const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
			let node = walker.nextNode();
			while (node) {
				if (/\b2\s+minutes\b/.test(node.nodeValue ?? '')) {
					node.nodeValue = (node.nodeValue ?? '').replace(/\b2\s+minutes\b/g, 'a few seconds');
				}
				node = walker.nextNode();
			}
		});

		return trySkipEraseStep(dialog, collectRoots(dialog));
	};

	return observeUntilApplied(dialog, applyCustomCopy, 120000, true);
}

function customizeNoPortPickedDialog(dialog: CustomDialog): () => void {
	if (dialog.__customNoPortCopyInit) {
		return () => undefined;
	}

	dialog.__customNoPortCopyInit = true;
	const desiredContentHtml = `
		<div>If your train map did not appear in the port list, try these steps:</div>
		<ol>
			<li>Make sure your train map is connected to this computer (the same one running this setup page).</li>
			<li>If you have a Melbourne or Auckland train map, make sure it is connected to the left USB port. (Down from Werribee or near Henderson respectively)</li>
			<li>Use the supplied USB cable, or another USB data cable (some cables are power-only and will not show a serial port).</li>
			<li>Try a different USB port on your computer, then click Try Again.</li>
			<li>Unplug and reconnect the map, wait a few seconds, and reopen the port picker.</li>
		</ol>
	`.trim();

	const applyCustomCopy = (): boolean => {
		const root = dialog.shadowRoot;
		if (!root) {
			return false;
		}

		const heading = root.querySelector('[slot="headline"]');
		const contentSlot = root.querySelector('[slot="content"]');
		if (!heading || !contentSlot) {
			return false;
		}

		heading.textContent = 'No port selected';
		contentSlot.innerHTML = desiredContentHtml;
		return true;
	};

	return observeUntilApplied(dialog, applyCustomCopy, 3000, false);
}

export function initDialogCustomizations(): () => void {
	const cleanups: Array<() => void> = [];
	const dialogConfigs = [
		{ selector: 'ewt-no-port-picked-dialog', customize: customizeNoPortPickedDialog },
		{ selector: 'ewt-install-dialog', customize: customizeInstallDialog },
	];

	const applyToDialog = (dialog: Element, customize: (dialog: CustomDialog) => () => void): void => {
		cleanups.push(customize(dialog as CustomDialog));
	};

	const processAddedNode = (addedNode: Node): void => {
		if (!(addedNode instanceof Element)) {
			return;
		}

		dialogConfigs.forEach(({ selector, customize }) => {
			if (addedNode.matches(selector)) {
				applyToDialog(addedNode, customize);
			}
			addedNode.querySelectorAll(selector).forEach((dialog) => applyToDialog(dialog, customize));
		});
	};

	dialogConfigs.forEach(({ selector, customize }) => {
		document.querySelectorAll(selector).forEach((dialog) => applyToDialog(dialog, customize));
	});

	const observer = new MutationObserver((mutations) => {
		mutations.forEach((mutation) => mutation.addedNodes.forEach(processAddedNode));
	});
	observer.observe(document.body, { childList: true, subtree: true });

	return () => {
		observer.disconnect();
		cleanups.forEach((cleanup) => cleanup());
	};
}
