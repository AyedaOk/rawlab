type SaveTask = () => Promise<unknown>;

type RevisionTask = {
  revision: number;
  task: SaveTask;
};

type Entry = {
  revision: number;
  timer?: number;
  pending?: RevisionTask;
  running?: Promise<void>;
  blocked?: unknown;
};

export class PersistenceQueue {
  private entries = new Map<string, Entry>();
  private epoch = 0;

  constructor(
    private readonly onError: (key: string, error: unknown) => void,
    private readonly onSuccess?: (key: string) => void,
  ) {}

  schedule(key: string, delay: number, task: SaveTask) {
    const entry = this.entries.get(key) ?? { revision: 0 };
    if (entry.timer !== undefined) window.clearTimeout(entry.timer);
    entry.pending = { revision: ++entry.revision, task };
    this.epoch++;
    if (!entry.running && entry.blocked === undefined) {
      entry.timer = window.setTimeout(() => {
        entry.timer = undefined;
        void this.drain(key, entry);
      }, delay);
    } else entry.timer = undefined;
    this.entries.set(key, entry);
  }

  async flush(key: string): Promise<boolean> {
    const entry = this.entries.get(key);
    if (!entry) return true;
    if (entry.timer !== undefined) {
      window.clearTimeout(entry.timer);
      entry.timer = undefined;
    }
    return this.drain(key, entry);
  }

  async flushAll() {
    while (true) {
      const epoch = this.epoch;
      const results = await Promise.all([...this.entries.keys()].map((key) => this.flush(key)));
      if (results.some((saved) => !saved)) return false;
      if (epoch === this.epoch && [...this.entries.values()].every((entry) =>
        !entry.pending && !entry.running && entry.timer === undefined)) return true;
    }
  }

  async retry(key: string): Promise<boolean> {
    const entry = this.entries.get(key);
    if (!entry) return true;
    entry.blocked = undefined;
    return this.flush(key);
  }

  failedKeys() {
    return [...this.entries].filter(([, entry]) => entry.blocked !== undefined).map(([key]) => key);
  }

  private async drain(key: string, entry: Entry): Promise<boolean> {
    if (entry.blocked !== undefined) return false;
    while (true) {
      if (entry.running) await entry.running;
      if (entry.blocked !== undefined) return false;
      const pending = entry.pending;
      if (!pending) {
        if (entry.timer === undefined) this.entries.delete(key);
        return true;
      }
      if (entry.timer !== undefined) {
        window.clearTimeout(entry.timer);
        entry.timer = undefined;
      }
      entry.pending = undefined;
      entry.running = (async () => {
      try {
          await pending.task();
          this.onSuccess?.(key);
      } catch (error) {
          entry.pending ??= pending;
          entry.blocked = error;
        this.onError(key, error);
      } finally {
        entry.running = undefined;
      }
    })();
      await entry.running;
    }
  }
}
