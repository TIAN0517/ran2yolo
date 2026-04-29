"""
高容错有限状态机 (FSM) + 看门狗定时器 (Watchdog Timer)
Python 实现 - 用于游戏 Bot

特性:
1. 每个状态可设置最大超时时间
2. 超时强制中断并跳转 Recovery 状态
3. Recovery 状态实现盲按 ESC、随机移动等脱困逻辑
4. 确认安全后重置回 Idle
"""

import time
import random
import threading
import logging
from enum import Enum, auto
from dataclasses import dataclass, field
from typing import Callable, Optional, Dict, Any
from collections import deque

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
logger = logging.getLogger('FSM_Watchdog')


class State(Enum):
    """FSM 状态枚举"""
    IDLE = auto()
    HUNTING = auto()
    ATTACKING = auto()
    USING_SKILL = auto()
    PICKUP = auto()
    TOWN_SUPPLY = auto()
    DEAD = auto()
    RETURNING = auto()
    TRAVELING = auto()
    RECOVERY = auto()        # 异常恢复状态
    SAFE_CHECK = auto()      # 安全检查状态


@dataclass
class StateConfig:
    """状态配置"""
    name: str
    timeout_sec: float = 30.0      # 默认超时时间
    max_retries: int = 3           # 最大重试次数
    on_timeout_goto: State = State.RECOVERY  # 超时跳转状态


@dataclass
class TransitionResult:
    """状态转换结果"""
    success: bool
    next_state: State
    message: str = ""
    data: Dict[str, Any] = field(default_factory=dict)


class WatchdogTimer:
    """看门狗定时器"""
    def __init__(self, timeout_sec: float, callback: Callable[[], None]):
        self.timeout_sec = timeout_sec
        self.callback = callback
        self._start_time: float = 0
        self._running: bool = False
        self._expired: bool = False
        self._lock = threading.Lock()

    def start(self):
        """启动看门狗"""
        with self._lock:
            self._start_time = time.time()
            self._running = True
            self._expired = False
        logger.debug(f"Watchdog started: timeout={self.timeout_sec}s")

    def stop(self):
        """停止看门狗"""
        with self._lock:
            self._running = False

    def check(self) -> bool:
        """
        检查是否超时
        返回 True 表示已超时，需要触发回调
        """
        if not self._running:
            return False

        elapsed = time.time() - self._start_time
        if elapsed >= self.timeout_sec and not self._expired:
            self._expired = True
            logger.warning(f"Watchdog expired after {elapsed:.2f}s")
            return True
        return False

    def reset(self):
        """重置看门狗"""
        with self._lock:
            self._start_time = time.time()
            self._expired = False

    @property
    def remaining_sec(self) -> float:
        """剩余时间"""
        if not self._running:
            return 0
        return max(0, self.timeout_sec - (time.time() - self._start_time))


class RecoveryController:
    """
    异常恢复控制器
    实现盲按 ESC、随机移动等强制脱困逻辑
    """

    def __init__(self, input_sender, vision_checker):
        self.input = input_sender
        self.vision = vision_checker
        self.recovery_attempts: int = 0
        self.max_recovery_attempts: int = 5
        self.action_history = deque(maxlen=10)  # 记录最近动作

    def execute_recovery(self) -> bool:
        """
        执行恢复程序
        返回 True 表示已恢复到安全状态
        """
        logger.info("=== Starting Recovery Procedure ===")
        self.recovery_attempts += 1

        # 恢复流程：ESC -> 等待 -> 随机移动 -> 检查
        recovery_steps = [
            ("ESC", self._press_esc),
            ("WAIT_1S", self._wait_random),
            ("MOVE_FORWARD", self._random_move_forward),
            ("WAIT_2S", self._wait_longer),
            ("ESC_AGAIN", self._press_esc),
            ("CHECK_SAFE", self._check_safe),
        ]

        for step_name, step_func in recovery_steps:
            logger.info(f"Recovery step: {step_name}")
            self.action_history.append(step_name)

            if step_name == "CHECK_SAFE":
                if step_func():
                    logger.info("=== Recovery SUCCESS: Safe state confirmed ===")
                    return True
            else:
                step_func()
                time.sleep(random.uniform(0.1, 0.3))

        logger.warning(f"Recovery attempt {self.recovery_attempts} failed")
        return False

    def _press_esc(self):
        """盲按 ESC"""
        logger.info("Pressing ESC (blind)")
        self.input.press_key("ESC")
        self.action_history.append("ESC")

    def _wait_random(self):
        """随机等待 1-2 秒"""
        wait_time = random.uniform(1.0, 2.0)
        logger.debug(f"Waiting {wait_time:.2f}s")
        time.sleep(wait_time)

    def _wait_longer(self):
        """等待 2-3 秒"""
        wait_time = random.uniform(2.0, 3.0)
        logger.debug(f"Waiting {wait_time:.2f}s")
        time.sleep(wait_time)

    def _random_move_forward(self):
        """随机移动"""
        directions = ["W", "A", "S", "D", "W", "D"]  # 偏向安全和前方
        direction = random.choice(directions)
        duration = random.uniform(0.5, 1.5)

        logger.info(f"Moving {direction} for {duration:.2f}s")
        self.input.hold_key(direction, duration)
        self.action_history.append(f"MOVE_{direction}")

    def _check_safe(self) -> bool:
        """检查是否安全"""
        logger.info("Checking if state is safe...")

        # 视觉检查
        if self.vision:
            is_safe = self.vision.check_safe_state()
            if not is_safe:
                logger.warning("Vision: Not in safe state")
                return False

        # 检查是否卡住（通过位置变化）
        if self._is_stuck():
            logger.warning("Position check: Appears stuck")
            return False

        # 检查 HP 是否异常低
        if self._check_low_hp():
            logger.warning("HP check: Too low")
            return False

        logger.info("All safety checks passed")
        return True

    def _is_stuck(self) -> bool:
        """检测是否卡住（简化版）"""
        # TODO: 实现位置变化检测
        return False

    def _check_low_hp(self) -> bool:
        """检测 HP 是否过低"""
        # TODO: 实现 HP 检查
        return False

    def reset(self):
        """重置恢复状态"""
        self.recovery_attempts = 0
        self.action_history.clear()


class StateHandler:
    """状态处理器基类"""

    def __init__(self, name: str, config: StateConfig):
        self.name = name
        self.config = config
        self.enter_time: float = 0
        self.context: Dict[str, Any] = {}

    def on_enter(self, prev_state: Optional[State] = None):
        """进入状态"""
        self.enter_time = time.time()
        self.context.clear()
        logger.info(f"[{self.name}] Entered (from {prev_state})")

    def on_exit(self, next_state: State):
        """离开状态"""
        elapsed = time.time() - self.enter_time
        logger.info(f"[{self.name}] Exiting after {elapsed:.2f}s (to {next_state})")

    def update(self) -> TransitionResult:
        """
        更新状态
        返回转换结果
        """
        raise NotImplementedError

    def on_timeout(self) -> TransitionResult:
        """
        超时处理
        """
        logger.warning(f"[{self.name}] Timeout! Forcing transition to RECOVERY")
        return TransitionResult(
            success=False,
            next_state=State.RECOVERY,
            message=f"Timeout after {self.config.timeout_sec}s"
        )

    @property
    def elapsed_sec(self) -> float:
        """已运行时间"""
        return time.time() - self.enter_time


# ============== 具体状态处理器 ==============

class IdleHandler(StateHandler):
    """空闲状态"""

    def __init__(self, config: StateConfig, bot):
        super().__init__("IDLE", config)
        self.bot = bot

    def update(self) -> TransitionResult:
        if self.bot.is_hunting_enabled:
            return TransitionResult(True, State.HUNTING, "Starting hunt")
        return TransitionResult(True, State.IDLE, "Waiting")


class HuntingHandler(StateHandler):
    """狩猎状态"""

    def __init__(self, config: StateConfig, bot):
        super().__init__("HUNTING", config)
        self.bot = bot

    def update(self) -> TransitionResult:
        # 检查目标
        target = self.bot.find_nearest_monster()
        if target:
            self.context['target'] = target
            return TransitionResult(True, State.ATTACKING, f"Found target: {target.id}")

        # 检查是否需要补给
        if self.bot.needs_supply():
            return TransitionResult(True, State.TOWN_SUPPLY, "Need supplies")

        # 检查死亡
        if self.bot.is_dead():
            return TransitionResult(True, State.DEAD, "Player died")

        # 移动到狩猎点
        self.bot.move_to_hunting_spot()
        return TransitionResult(True, State.HUNTING, "Moving to spot")


class AttackHandler(StateHandler):
    """攻击状态"""

    def __init__(self, config: StateConfig, bot):
        super().__init__("ATTACKING", config)
        self.bot = bot

    def update(self) -> TransitionResult:
        target = self.context.get('target')

        # 目标已死亡
        if not target or not self.bot.is_valid_target(target):
            return TransitionResult(True, State.HUNTING, "Target gone")

        # 目标已死，捡物品
        if self.bot.is_target_dead(target):
            return TransitionResult(True, State.PICKUP, "Target dead, picking up")

        # 攻击
        if not self.bot.is_in_attack_range(target):
            self.bot.move_to(target)
            return TransitionResult(True, State.ATTACKING, "Moving to target")

        self.bot.attack(target)
        return TransitionResult(True, State.ATTACKING, "Attacking")


class RecoveryHandler(StateHandler):
    """恢复状态 - 异常脱困"""

    def __init__(self, config: StateConfig, recovery_ctrl: RecoveryController):
        super().__init__("RECOVERY", config)
        self.recovery = recovery_ctrl
        self.recovery_complete = False

    def on_enter(self, prev_state: Optional[State] = None):
        super().on_enter(prev_state)
        self.recovery_complete = False

    def update(self) -> TransitionResult:
        # 执行恢复程序
        if self.recovery.execute_recovery():
            self.recovery.reset()
            return TransitionResult(True, State.IDLE, "Recovery successful")

        # 恢复失败
        if self.recovery.recovery_attempts >= self.recovery.max_recovery_attempts:
            logger.critical("Max recovery attempts reached! Full reset required")
            return TransitionResult(False, State.IDLE, "Max retries, reset to idle")

        # 再次尝试恢复
        return TransitionResult(True, State.RECOVERY, "Recovery retry")


class FSMWithWatchdog:
    """
    带看门狗的有限状态机
    """

    def __init__(self, bot, input_sender, vision_checker):
        self.bot = bot
        self.current_state: State = State.IDLE
        self.handlers: Dict[State, StateHandler] = {}
        self.watchdog: Optional[WatchdogTimer] = None
        self.running: bool = False
        self.tick_interval: float = 0.1  # 100ms tick

        # 恢复控制器
        self.recovery = RecoveryController(input_sender, vision_checker)

        # 初始化状态配置
        self._init_state_configs()

    def _init_state_configs(self):
        """初始化状态配置"""

        configs = {
            State.IDLE: StateConfig("IDLE", timeout_sec=300),
            State.HUNTING: StateConfig("HUNTING", timeout_sec=60),
            State.ATTACKING: StateConfig("ATTACKING", timeout_sec=30),
            State.USING_SKILL: StateConfig("USING_SKILL", timeout_sec=15),
            State.PICKUP: StateConfig("PICKUP", timeout_sec=20),
            State.TOWN_SUPPLY: StateConfig("TOWN_SUPPLY", timeout_sec=120),
            State.DEAD: StateConfig("DEAD", timeout_sec=10),
            State.RETURNING: StateConfig("RETURNING", timeout_sec=60),
            State.TRAVELING: StateConfig("TRAVELING", timeout_sec=180),
            State.RECOVERY: StateConfig("RECOVERY", timeout_sec=60),
            State.SAFE_CHECK: StateConfig("SAFE_CHECK", timeout_sec=30),
        }

        # 注册处理器
        self.handlers[State.IDLE] = IdleHandler(configs[State.IDLE], self.bot)
        self.handlers[State.HUNTING] = HuntingHandler(configs[State.HUNTING], self.bot)
        self.handlers[State.ATTACKING] = AttackHandler(configs[State.ATTACKING], self.bot)
        self.handlers[State.RECOVERY] = RecoveryHandler(configs[State.RECOVERY], self.recovery)

    def start(self):
        """启动 FSM"""
        self.running = True
        self._transition_to(State.IDLE)
        logger.info("FSM started")

    def stop(self):
        """停止 FSM"""
        self.running = False
        if self.watchdog:
            self.watchdog.stop()
        logger.info("FSM stopped")

    def _transition_to(self, new_state: State):
        """状态转换"""
        old_handler = self.handlers.get(self.current_state)
        new_handler = self.handlers.get(new_state)

        if old_handler:
            old_handler.on_exit(new_state)

        self.current_state = new_state

        if new_handler:
            new_handler.on_enter(self.current_state)

            # 重置看门狗
            config = new_handler.config
            if self.watchdog:
                self.watchdog.stop()

            self.watchdog = WatchdogTimer(
                timeout_sec=config.timeout_sec,
                callback=lambda: self._on_watchdog_expired()
            )
            self.watchdog.start()

    def _on_watchdog_expired(self):
        """看门狗超时回调"""
        logger.warning(f"Watchdog expired in state {self.current_state}")
        handler = self.handlers.get(self.current_state)
        if handler:
            result = handler.on_timeout()
            self._transition_to(result.next_state)

    def tick(self):
        """FSM 主循环"""
        if not self.running:
            return

        # 检查看门狗
        if self.watchdog and self.watchdog.check():
            logger.warning("Watchdog expired in tick")

        # 更新当前状态
        handler = self.handlers.get(self.current_state)
        if handler:
            result = handler.update()

            if result.next_state != self.current_state:
                logger.info(f"Transition: {self.current_state} -> {result.next_state}")
                self._transition_to(result.next_state)

    def run(self):
        """运行 FSM（阻塞）"""
        self.start()
        try:
            while self.running:
                self.tick()
                time.sleep(self.tick_interval)
        except KeyboardInterrupt:
            logger.info("FSM interrupted")
        finally:
            self.stop()

    def get_status(self) -> Dict[str, Any]:
        """获取状态机状态"""
        handler = self.handlers.get(self.current_state)
        return {
            "current_state": self.current_state.name,
            "elapsed_sec": handler.elapsed_sec if handler else 0,
            "timeout_sec": handler.config.timeout_sec if handler else 0,
            "watchdog_remaining": self.watchdog.remaining_sec if self.watchdog else 0,
            "recovery_attempts": self.recovery.recovery_attempts,
        }


# ============== 使用示例 ==============

class MockInputSender:
    """模拟输入发送器"""
    def press_key(self, key: str):
        logger.info(f"Press: {key}")

    def hold_key(self, key: str, duration: float):
        logger.info(f"Hold: {key} for {duration}s")


class MockVisionChecker:
    """模拟视觉检查器"""
    def check_safe_state(self) -> bool:
        logger.info("Vision: Checking safe state...")
        return True


class MockBot:
    """模拟游戏 Bot"""
    def __init__(self):
        self.is_hunting_enabled = False
        self.position = (0, 0)

    def is_dead(self) -> bool:
        return False

    def needs_supply(self) -> bool:
        return False

    def find_nearest_monster(self):
        return None

    def is_valid_target(self, target) -> bool:
        return True

    def is_target_dead(self, target) -> bool:
        return False

    def is_in_attack_range(self, target) -> bool:
        return True

    def move_to(self, target):
        pass

    def attack(self, target):
        pass

    def move_to_hunting_spot(self):
        pass


if __name__ == "__main__":
    # 测试 FSM
    bot = MockBot()
    input_sender = MockInputSender()
    vision_checker = MockVisionChecker()

    fsm = FSMWithWatchdog(bot, input_sender, vision_checker)

    # 启用狩猎
    bot.is_hunting_enabled = True

    # 运行 10 秒
    import signal
    def signal_handler(sig, frame):
        print("\nStopping FSM...")
        fsm.stop()
        exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    print("Running FSM for 10 seconds...")
    start = time.time()
    fsm.start()
    while time.time() - start < 10:
        fsm.tick()
        time.sleep(0.1)

    print(f"Status: {fsm.get_status()}")
    fsm.stop()
