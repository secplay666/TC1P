# -*- coding: utf-8 -*-
from __future__ import annotations

from contextlib import closing
from dataclasses import dataclass
import datetime as _dt
from pathlib import Path
import secrets
import sqlite3
from typing import Optional


DB_PATH = Path(__file__).with_name("provisioning.db")


@dataclass(frozen=True)
class IdentityAllocation:
    product_sn: int
    date_code: str
    seq_no: int
    terminal_sn: int
    random_value: int
    reserved: int
    unique_id: bytes


def today_date_code() -> str:
    today = _dt.date.today()
    return f"{today.year % 100:02d}{today.month:02d}{today.day:02d}"


def normalize_date_code(value: str) -> str:
    text = value.strip().lower()
    if text.startswith("0x"):
        text = text[2:]
    if len(text) != 6 or not text.isdigit():
        raise ValueError("日期必须是 yymmdd，例如 260531")
    yy = int(text[0:2])
    mm = int(text[2:4])
    dd = int(text[4:6])
    _dt.date(2000 + yy, mm, dd)
    return text


def terminal_sn_from_date_seq(date_code: str, seq_no: int) -> int:
    date_code = normalize_date_code(date_code)
    if seq_no < 1 or seq_no > 99:
        raise ValueError("终端序号 ss 当前限定为 01 到 99")
    return int(f"{date_code}{seq_no:02d}", 16)


def build_unique_id(product_sn: int, terminal_sn: int, random_value: int, reserved: int = 0) -> bytes:
    return (
        int(product_sn & 0xFFFFFFFF).to_bytes(4, "little")
        + int(terminal_sn & 0xFFFFFFFF).to_bytes(4, "little")
        + int(random_value & 0xFFFFFFFF).to_bytes(4, "little")
        + int(reserved & 0xFFFFFFFF).to_bytes(4, "little")
    )


class ProvisionDatabase:
    def __init__(self, path: Path = DB_PATH) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._init_schema()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.path)
        conn.row_factory = sqlite3.Row
        return conn

    def _init_schema(self) -> None:
        with closing(self._connect()) as conn:
            conn.execute(
                """
                create table if not exists provision_records (
                    id integer primary key autoincrement,
                    created_at text not null,
                    device_address text,
                    product_sn integer not null,
                    date_code text not null,
                    seq_no integer not null,
                    terminal_sn integer not null,
                    random_value integer not null,
                    reserved integer not null default 0,
                    unique_id text not null,
                    eid text,
                    short_id integer,
                    flags integer,
                    status text not null,
                    note text
                )
                """
            )
            conn.execute(
                "create unique index if not exists idx_provision_unique_id on provision_records(unique_id)"
            )
            conn.execute(
                """
                create unique index if not exists idx_provision_product_terminal
                on provision_records(product_sn, terminal_sn)
                """
            )
            conn.execute(
                """
                create index if not exists idx_provision_product_date_seq
                on provision_records(product_sn, date_code, seq_no)
                """
            )
            conn.commit()

    def get_last_seq(self, product_sn: int, date_code: str) -> int:
        date_code = normalize_date_code(date_code)
        with closing(self._connect()) as conn:
            row = conn.execute(
                """
                select max(seq_no) as last_seq
                from provision_records
                where product_sn = ? and date_code = ?
                """,
                (product_sn & 0xFFFFFFFF, date_code),
            ).fetchone()
        return int(row["last_seq"] or 0)

    def allocate_next(self, product_sn: int, date_code: str, reserved: int = 0) -> IdentityAllocation:
        date_code = normalize_date_code(date_code)
        seq_no = self.get_last_seq(product_sn, date_code) + 1
        terminal_sn = terminal_sn_from_date_seq(date_code, seq_no)
        random_value = self._new_random_value()
        unique_id = build_unique_id(product_sn, terminal_sn, random_value, reserved)
        return IdentityAllocation(
            product_sn=product_sn & 0xFFFFFFFF,
            date_code=date_code,
            seq_no=seq_no,
            terminal_sn=terminal_sn,
            random_value=random_value,
            reserved=reserved & 0xFFFFFFFF,
            unique_id=unique_id,
        )

    def record_success(
        self,
        allocation: IdentityAllocation,
        *,
        device_address: Optional[str],
        eid: str,
        short_id: int,
        flags: int,
        status: str,
        note: str = "",
    ) -> None:
        now = _dt.datetime.now().isoformat(timespec="seconds")
        with closing(self._connect()) as conn:
            conn.execute(
                """
                insert into provision_records (
                    created_at, device_address, product_sn, date_code, seq_no,
                    terminal_sn, random_value, reserved, unique_id, eid, short_id,
                    flags, status, note
                ) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    now,
                    device_address,
                    allocation.product_sn,
                    allocation.date_code,
                    allocation.seq_no,
                    allocation.terminal_sn,
                    allocation.random_value,
                    allocation.reserved,
                    allocation.unique_id.hex().upper(),
                    eid,
                    short_id & 0xFFFFFFFF,
                    flags & 0xFF,
                    status,
                    note,
                ),
            )
            conn.commit()

    def find_by_unique_id(self, unique_id_hex: str) -> Optional[sqlite3.Row]:
        with closing(self._connect()) as conn:
            return conn.execute(
                "select * from provision_records where unique_id = ?",
                (unique_id_hex.upper(),),
            ).fetchone()

    def recent_records(self, limit: int = 30) -> list[sqlite3.Row]:
        with closing(self._connect()) as conn:
            return list(
                conn.execute(
                    """
                    select * from provision_records
                    order by id desc
                    limit ?
                    """,
                    (limit,),
                )
            )

    @staticmethod
    def _new_random_value() -> int:
        while True:
            value = secrets.randbits(32)
            if value not in (0, 0xFFFFFFFF):
                return value
