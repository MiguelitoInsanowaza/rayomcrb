-- =====================================================================
--  Minecraft SS Tool - base de datos (Supabase / PostgreSQL)
--  Supabase > SQL Editor > New query > pega TODO esto > Run
--  Se puede ejecutar varias veces sin romper nada.
-- =====================================================================

create extension if not exists pgcrypto;

-- ---------------------------- Tablas ----------------------------
create table if not exists public.licenses (
  id           uuid primary key default gen_random_uuid(),
  key          text not null unique,
  note         text,
  max_uses     integer check (max_uses is null or max_uses > 0),   -- null = ilimitado
  uses         integer not null default 0,
  expires_at   timestamptz,                                       -- null = no vence
  lock_hwid    boolean not null default false,                    -- vincular a la primera PC que la use
  hwid         text,
  active       boolean not null default true,
  last_used_at timestamptz,
  created_at   timestamptz not null default now()
);

create table if not exists public.scans (
  id           uuid primary key default gen_random_uuid(),
  license_id   uuid references public.licenses(id) on delete set null,
  license_key  text,
  created_at   timestamptz not null default now(),
  hwid         text,
  pc_name      text,
  windows_user text,
  tool_version text,
  alerts       integer not null default 0,
  flags        jsonb not null default '[]'::jsonb,
  report       text
);
create index if not exists scans_created_at_idx on public.scans (created_at desc);

-- Reglas extra que descarga el programa (mods, paginas web y procesos prohibidos)
create table if not exists public.rules (
  id         uuid primary key default gen_random_uuid(),
  type       text not null check (type in ('mod', 'web', 'process')),
  name       text not null,
  tokens     text not null,          -- palabras separadas por coma, ej: freecam,free_cam
  active     boolean not null default true,
  created_at timestamptz not null default now()
);

-- ---------------------------- Seguridad ----------------------------
-- Solo los usuarios con sesion (los admins que crees en Authentication > Users)
-- pueden ver y editar. El programa (.exe) NO tiene acceso directo a las tablas:
-- solo puede llamar a las 3 funciones de abajo.
alter table public.licenses enable row level security;
alter table public.scans    enable row level security;
alter table public.rules    enable row level security;

drop policy if exists "admins_licenses" on public.licenses;
create policy "admins_licenses" on public.licenses for all to authenticated using (true) with check (true);
drop policy if exists "admins_scans" on public.scans;
create policy "admins_scans" on public.scans for all to authenticated using (true) with check (true);
drop policy if exists "admins_rules" on public.rules;
create policy "admins_rules" on public.rules for all to authenticated using (true) with check (true);

-- ---------------------------- Funciones ----------------------------
create or replace function public._license_error(l public.licenses, p_hwid text)
returns text
language sql
stable
as $$
  select case
    when not l.active then 'La licencia está desactivada'
    when l.expires_at is not null and l.expires_at < now() then 'La licencia está vencida'
    when l.max_uses is not null and l.uses >= l.max_uses then 'La licencia ya no tiene usos disponibles'
    when l.lock_hwid and l.hwid is not null and l.hwid <> coalesce(p_hwid, '') then 'La licencia está vinculada a otra PC'
    else null
  end
$$;

create or replace function public.check_license(p_key text, p_hwid text)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  l   public.licenses;
  err text;
begin
  select * into l from public.licenses where key = upper(trim(p_key));
  if not found then
    return jsonb_build_object('ok', false, 'msg', 'La licencia no existe');
  end if;
  err := public._license_error(l, p_hwid);
  if err is not null then
    return jsonb_build_object('ok', false, 'msg', err);
  end if;
  return jsonb_build_object('ok', true, 'msg', 'Licencia válida');
end;
$$;

create or replace function public.get_rules(p_key text, p_hwid text)
returns text
language plpgsql
security definer
set search_path = public
as $$
declare
  l public.licenses;
begin
  select * into l from public.licenses where key = upper(trim(p_key));
  if not found or public._license_error(l, p_hwid) is not null then
    return '';
  end if;
  return coalesce((
    select string_agg(type || '|' || replace(name, '|', '/') || '|' || lower(tokens), E'\n')
    from public.rules
    where active
  ), '');
end;
$$;

create or replace function public.submit_scan(
  p_key text, p_hwid text, p_pc text, p_user text, p_version text,
  p_alerts integer, p_flags jsonb, p_report text)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  l      public.licenses;
  err    text;
  new_id uuid;
begin
  select * into l from public.licenses where key = upper(trim(p_key)) for update;
  if not found then
    return jsonb_build_object('ok', false, 'msg', 'La licencia no existe');
  end if;
  err := public._license_error(l, p_hwid);
  if err is not null then
    return jsonb_build_object('ok', false, 'msg', err);
  end if;

  update public.licenses
     set uses = uses + 1,
         last_used_at = now(),
         hwid = case when lock_hwid and hwid is null then left(p_hwid, 100) else hwid end
   where id = l.id;

  insert into public.scans (license_id, license_key, hwid, pc_name, windows_user, tool_version, alerts, flags, report)
  values (l.id, l.key, left(p_hwid, 100), left(p_pc, 100), left(p_user, 100), left(p_version, 20),
          greatest(coalesce(p_alerts, 0), 0), coalesce(p_flags, '[]'::jsonb), left(p_report, 3000000))
  returning id into new_id;

  return jsonb_build_object('ok', true, 'id', new_id);
end;
$$;

revoke all on function public._license_error(public.licenses, text) from public, anon, authenticated;
grant execute on function public.check_license(text, text) to anon, authenticated;
grant execute on function public.get_rules(text, text) to anon, authenticated;
grant execute on function public.submit_scan(text, text, text, text, text, integer, jsonb, text) to anon, authenticated;
