#!/usr/bin/env python3
"""Persist a random, reproducible approved-track draw without repeats within a batch."""
from pathlib import Path
import argparse,json,random,secrets
APPROVED_TRACKS=[
 'We Will Stop Them / Deception (OST Version)','Die (Remastered)','ReCon (Remastered)',
 'Iron Fist (Remastered)','Depth Charge (Remastered)','Creeping Upon (Remastered)',
 'Airstrike (Remastered)','Fight Win Prevail (Remastered)','March To Doom (Remastered)',
]
def choose(subjects,seed):
 if len(subjects)!=len(set(subjects)):raise ValueError('Subject identifiers must be unique')
 rng=random.Random(seed);picked=[]
 while len(picked)<len(subjects):
  pool=rng.sample(APPROVED_TRACKS,len(APPROVED_TRACKS))
  if picked and pool[0]==picked[-1]:pool[0],pool[1]=pool[1],pool[0]
  picked+=pool[:len(subjects)-len(picked)]
 return dict(zip(subjects,picked))
def main():
 p=argparse.ArgumentParser();p.add_argument('plan',type=Path);p.add_argument('subjects',nargs='+');p.add_argument('--seed',type=int);a=p.parse_args()
 if a.plan.exists():
  saved=json.loads(a.plan.read_text());assert set(saved['selection'])==set(a.subjects),'Existing batch differs; use a new plan path'
  assert len(set(saved['selection'].values()))==len(a.subjects) or len(a.subjects)>len(APPROVED_TRACKS)
  print(json.dumps(saved['selection'],indent=2));return
 seed=a.seed if a.seed is not None else secrets.randbits(64)
 result=dict(seed=seed,approved_tracks=APPROVED_TRACKS,selection=choose(a.subjects,seed),policy='Random shuffle without replacement; preserve saved selection for technical rerenders')
 a.plan.parent.mkdir(parents=True,exist_ok=True);a.plan.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['selection'],indent=2))
if __name__=='__main__':main()
